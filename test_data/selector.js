try {
	var selected = document.querySelectorAll(".selectme");
	for (var i in selected) {
		console.log('handle', selected[i].handle);
		console.log('attribute', selected[i].getAttribute('attribute'));
	}
} catch (e) {
	console.log(e);
}

function lengthCheck() {
    var name = this.getAttribute("name");
    var value = this.getAttribute("value");
    if (value.length > 100) {
        console.log("Input " + name + " has too much text.");
    }
}

var inputs = document.querySelectorAll("input");
for (var i = 0; i < inputs.length; i++) {
    inputs[i].addEventListener("keydown", lengthCheck);
}
