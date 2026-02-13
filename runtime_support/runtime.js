LISTENERS = {}

function Event(type) {
	this.type = type;
	this.do_default = true;
	this.propagate = true;
}

Event.prototype.preventDefault = function() {
	this.do_default = false;
}

Event.prototype.stopPropagation = function() {
	this.propagate = false;
}

function Node(handle) {
	this.handle = handle;
}

Node.prototype.addEventListener = function(type, listener) {
	if (!LISTENERS[this.handle]) LISTENERS[this.handle] = {};
	var dict = LISTENERS[this.handle];
	if (!dict[type]) dict[type] = [];
	var list = dict[type];
	list.push(listener);
}

Node.prototype.dispatchEvent = function(evt) {
	var type = evt.type;
	var handle = this.handle;
	var list = (LISTENERS[handle] && LISTENERS[handle][type]) || [];
	for (var i = 0; i < list.length; i++) {
		list[i].call(this, evt);
	}
	return evt;
}

XHR_REQUESTS = {}

function XMLHttpRequest() {
	this.handle = Object.keys(XHR_REQUESTS).length;
	XHR_REQUESTS[this.handle] = this;
}

// todo: is_async is optional and defaults to true.
XMLHttpRequest.prototype.open = function(method, url, is_async) {
	this.is_async = is_async;
	this.method = method;
	this.url = url;
}

function __runXHROnload(body, handle) {
try {
	var obj = XHR_REQUESTS[handle];
	var evt = new Event('load');
	obj.responseText = body;
	if (obj.onload)
		obj.onload(evt);
} catch (e) {
	console.log('error in __runXMLOnload', e);
}
}

SET_TIMEOUT_REQUESTS = {}

function setTimeout(callback, time_delta) {
	var handle = Object.keys(SET_TIMEOUT_REQUESTS).length;
	SET_TIMEOUT_REQUESTS[handle] = callback;
	__internal.setTimeout(handle, time_delta);
}

function __runSetTimeout(handle) {
	var callback = SET_TIMEOUT_REQUESTS[handle];
	callback();
}
