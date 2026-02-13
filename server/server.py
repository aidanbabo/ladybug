import html
import random
import socket
import urllib.parse

ENTRIES = [
    ('No names. We are nameless!', 'cerealkiller'),
    ('HACK THE PLANET!!!', 'crashoverride'),
]
SESSIONS = {}
LOGINS = {
    '': '',
    'crashoverride': '0cool',
    'cerealkiller': 'emmanuel',
}

def form_decode(body):
    params = {}
    for field in body.split('&'):
        name, value = field.split('=', 1)
        name = urllib.parse.unquote_plus(name)
        value = urllib.parse.unquote_plus(value)
        params[name] = value
    return params


def add_entry(session, params):
    if 'nonce' not in session or 'nonce' not in params: return
    if session['nonce'] != params['nonce']: return
    if 'user' not in session: return
    # must server side validate
    if 'guest' in params and len(params['guest']) <= 100:
        ENTRIES.append((params['guest'], session['user']))


def secret():
    out = '<p>You found me!</p>'
    out += '<a href="/">Back to home</a>'
    return out


def show_comments(session):
    out = '<!doctype html>'
    out += '<link rel=stylesheet href=comment.css>'
    for entry, who in ENTRIES:
        out += '<p>' + html.escape(entry) + '\n'
        out += '<i>by ' + html.escape(who) + '</i></p>'

    if 'user' in session:
        nonce = str(random.random())[2:]
        session['nonce'] = nonce
        out += '<h1>Hello, ' + session['user'] + '</h1>'
        out += '<form action=add method=post>'
        out += '  <p><input name=guest></p>'
        out += '  <p><button>Sign the book!</button></p>'
        out += '  <input name=nonce type=hidden value=' + nonce + '>'
        out += '</form>'
    else:
        out += '<a href=/login>Sign in to write in the guest book</a>'

    out += '<script src=https://example.com/evil.js></script>'
    out += '<strong></strong>'
    out += '<script src=/comment.js></script>'
    out += '<form action=/ method=get>'
    out += '  <p><input name=location></p>'
    out += '  <p><button>Guess the secret location!</button></p>'
    out += '</form>'

    return out


def login_form(session):
    # not protected from CSRF! Will our browser save us?
    body = '<!doctype html>'
    body += '<form action=/ method=post>'
    body += '<p>Username: <input name=username></p>'
    body += '<p>Password: <input name=password type=password></p>'
    body += '<p><button>Log in</button></p>'
    body += '</form>'
    return body


def do_login(session, params):
    username = params.get('username')
    password = params.get('password')
    if username in LOGINS and LOGINS[username] == password:
        session['user'] = username
        return '200 OK', show_comments(session)
    else:
        out = '<!doctype html>'
        out += f'<h1>Invalid password for {username}</h1>'
        return '401 Unauthorized', out


def not_found(url, method):
    out = '<!doctype html>'
    out += f'<h1>{method} {url} not found!</h1>'
    return out


def do_request(session, method, url, headers, body):
    print(f'{method} {url}')
    for k, v in headers.items(): print(k, v)
    if body:
        print()
        print(body)
    print()
    print()

    if method == 'GET' and url == '/':
        return '200 OK', show_comments(session)
    elif method == 'GET' and url == '/comment.js':
        with open('comments.js') as f:
            return '200 OK', f.read()
    elif method == 'GET' and url == '/comment.css':
        with open('comments.css') as f:
            return '200 OK', f.read()
    elif method == 'GET' and url == '/login':
        return '200 OK', login_form(session)
    elif method == 'POST' and url == '/':
        params = form_decode(body)
        return do_login(session, params)
    elif method == 'GET' and url == '/?location=secret':
        return '200 OK', secret()
    elif method == 'POST' and url == '/add':
        params = form_decode(body)
        add_entry(session, params)
        return '200 OK', show_comments(session)
    else:
        return '404 Not Found', not_found(url, method)


def handle_connection(conx):
    req = conx.makefile('b')
    reqline = req.readline().decode('utf8')
    method, url, version = reqline.split(' ', 2)
    assert method in ['GET', 'POST']

    headers = {}
    while True:
        line = req.readline().decode('utf8')
        if line == '\r\n':
            break
        header, value = line.split(':', 1)
        headers[header.casefold()] = value.strip()

    if 'content-length' in headers:
        length = int(headers['content-length'])
        body = req.read(length).decode()
    else:
        body = None

    if 'cookie' in headers:
        token = headers['cookie'][len('token='):]
    else:
        token = str(random.random())[2:]
    session = SESSIONS.setdefault(token, {})

    status, body = do_request(session, method, url, headers, body)
    response = f'HTTP/1.0 {status}\r\n'
    response += f'Content-Length: {len(body.encode("utf8"))}\r\n'
    if 'cookie' not in headers:
        response += f'Set-Cookie: token={token}; SameSite=Lax\r\n'
    response += 'Content-Security-Policy: default-src http://localhost:8000\r\n'
    response += '\r\n' + body
    conx.send(response.encode('utf8'))
    conx.close()

if __name__ == '__main__':
    s = socket.socket(
            family=socket.AF_INET,
            type=socket.SOCK_STREAM,
            proto=socket.IPPROTO_TCP)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(('', 8000))
    s.listen()
    try:
        while True:
            conx, addr = s.accept()
            handle_connection(conx)
    except KeyboardInterrupt:
        s.close()
