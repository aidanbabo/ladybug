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
