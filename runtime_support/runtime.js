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

function XMLHttpRequest() {}

XMLHttpRequest.prototype.open = function(method, url, is_async) {
	if (is_async) throw Error('Asynchronous XHR is not supported');
	this.method = method;
	this.url = url;
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
