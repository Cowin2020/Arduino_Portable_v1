import "./plotly.min.js";

var records = new Array();

function $T(string) {
	return document.createTextNode(string);
}

function $E(name, attributes, styles, children) {
	var element = document.createElementNS(document.documentElement.namespaceURI, name);
	if (attributes != null)
		for (var name in attributes)
			element.setAttribute(name, attributes[name]);
	if (styles != null)
		for (var name in styles)
			element.style[name] = styles[name];
	if (Array.isArray(children))
		children.forEach(
			function (child) {
				return element.appendChild(child);
			}
		);
	return element;
}

document.body.style["margin"] = "1ex";

var $dashboard;

document.body.appendChild(
	$dashboard = $E("div",
		{"id": "dashboard"},
		{
			"margin": "2ex",
			"border": "double 8px #FC8",
			"border-radius": "4ex",
			"padding": "2ex",
			"width": "calc(100vw - 10ex - 16px)",
			"background-color": "#8AC",
		}
	)
);

function show_dashboard(row) {
	$dashboard.textContent = "";
	if (row == null)
		$dashboard.appendChild(
			$E("div",
				null,
				{
					"text-align": "center",
					"font-size": "4rem",
					"color": "#000",
					"text-shadow": "2px 2px 4px #FFF"
				},
				[$T("No data")]
			)
		);
	else {
		$dashboard.appendChild(
			$E("div",
				null,
				{
					"display": "flex",
					"flex-direction": "row",
					"flex-wrap": "wrap",
					"justify-content": "center",
					"column-gap": "1.5ex",
					"margin": "1ex",
					"text-align": "center",
					"font-size": "6ex",
					"color": "#CFD"
				},
				row[0].split("T").map(
					function (s) {
						return $E("span", null, null, [$T(s)]);
					}
				)
			)
		);
		function $item(title, index, unit) {
			return $E("div",
				null,
				{
					"display": "flex",
					"flex-flow": "column wrap",
					"justify-content": "center",
					"margin": "0.5rem",
					"border": "solid thin lightyellow",
					"padding": "1rem",
					"text-align": "center"
				},
				[
					$E("span", null, {"font-size": "2rem", "color": "#FED"}, [$T(title)]),
					$E("span", null, {"font-size": "3rem", "color": "snow"}, [
						$T(row[index]),
						$E("span", null, {"font-size": "1.5rem"}, [$T(unit)])
					])
				]
			)
		}
		$dashboard.appendChild(
			$E("div",
				null,
				{
					"display": "flex",
					"flex-flow": "row wrap",
					"justify-content": "center",
					"column-gap": "3ex"
				},
				[
					$item("Temperature", 1, "\u2103"),
					$item("Pressure", 2, "Pa"),
					$item("Humidity", 3, "%")
				]
			)
		);
	}
}

show_dashboard(null);

document.body.appendChild(
	$E("p",
		null,
		{
			"display": "flex",
			"flex-flow": "row wrap",
			"text-align": "center"
		},
		[
			$E("a",
				{"href": "setting.html"},
				{
					"margin": "1ex",
					"border": "solid thin gray",
					"padding": "1ex"
				},
				[$T("Settings")]
			),
			$E("a",
				{
					"href": "recent.csv",
					"download": ""
				},
				{
					"margin": "1ex",
					"border": "solid thin gray",
					"padding": "1ex"
				},
				[$T("Download recent data")]
			),
			$E("a",
				{
					"href": "all.csv",
					"download": ""
				},
				{
					"margin": "1ex",
					"border": "solid thin gray",
					"padding": "1ex"
				},
				[$T("Download all data")]
			)
		]
	)
);

var $reflesh, $auto;

document.body.appendChild(
	$reflesh = $E("form",
		null,
		{
			"margin-top": "2ex",
			"margin-bottom": "2ex"
		},
		[
			$E("button", {"type": "submit"}, null, [$T("Reflesh now")]),
			$E("label",
				null,
				{
					"margin-left": "2ex",
					"padding": "1ex"
				},
				[$T("Auto reflesh")]
			),
			$auto = $E("input", {"type": "checkbox"}, null, null)
		]
	)
);

var $plot_temperature, $plot_pressure, $plot_humidity;

void function () {
	document.body.appendChild(
		$plot_temperature = $E("div", null, {"width": "100%", "height": "90vh"})
	);
	document.body.appendChild(
		$plot_pressure = $E("div", null, {"width": "100%", "height": "90vh"})
	);
	document.body.appendChild(
		$plot_humidity = $E("div", null, {"width": "100%", "height": "90vh"})
	);
}();

function show_plot(rows) {
	if (!Array.isArray(rows)) {
		$plot_temperature.hidden = true;
		$plot_pressure.hidden = true;
		$plot_humidity.hidden = true;
	}
	else {
		$plot_temperature.hidden = false;
		$plot_pressure.hidden = false;
		$plot_humidity.hidden = false;
		function column(n) {
			return rows.map(function (row) {return row[n]});
		}
		var time = column(0);
		var layout = {
			margin: {autoexpand: false, r: 10}
		};
		var config = {
			responsive: true
		};
		Plotly.newPlot(
			$plot_temperature,
			{
				data: [{x: time, y: column(1)}],
				layout: {title: "Temperature (\u2103)", ...layout},
				config: config
			}
		);
		Plotly.newPlot(
			$plot_pressure,
			{
				data: [{x: time, y: column(2)}],
				layout: {title: "Pressure (Pa)", ...layout},
				config: config
			}
		);
		Plotly.newPlot(
			$plot_humidity,
			{
				data: [{x: time, y: column(3)}],
				layout: {title: "Humidity (%)", ...layout},
				config: config
			}
		);
	}
}

var $list;

document.body.appendChild(
	$E("table", null, {"width": "100%", "border-collapse": "collapse"}, [
		$E("thead", null, {"border-bottom-style": "solid"}, [
			$E("tr", null, null, [
				$E("th", null, null, [$T("Time")]),
				$E("th", null, null, [$T("Temperature")]),
				$E("th", null, null, [$T("Pressure")]),
				$E("th", null, null, [$T("Humidity")])
			])
		]),
		$list = $E("tbody")
	])
);

function load() {
	$list.textContent = null;
	var $loading = $E("p");
	$loading.appendChild($T("Loading..."));
	document.body.appendChild($loading);
	var xhr = new XMLHttpRequest();
	xhr.onloadend = function (event) {
		document.body.removeChild($loading);
		var text = xhr.responseText;
		if (text == null || xhr.status !== 200) {
			alert("Failed to load data");
			return;
		}
		var rows = text.split("\n").map(
			function (line) {
				return line.trim().split(",");
			}
		);
		rows.shift();
		while (rows.length && rows[rows.length - 1].length < 4)
			rows.pop();
		if (!(rows.length > 0)) {
			show_dashboard(null);
			show_plot(null);
			return;
		}
		show_dashboard(rows[rows.length - 1]);
		show_plot(rows);
		for (var i = 0; rows.length > i; ++i) {
			var fields = rows[rows.length - i - 1];
			$list.appendChild(
				$E("tr", null, null,
					fields.map(
						function (field) {
							return $E("td",
								null,
								{
									"border-style": "solid",
									"border-width": "thin",
									"text-align": "center"
								},
								[$T(field)]
							);
						}
					)
				)
			);
		}
	};
	xhr.open("GET", "/recent.csv", true);
	xhr.send(null);
}

$reflesh.addEventListener(
	"submit",
	function (event) {
		event.preventDefault();
		load();
	}
);

var timer = null;

$auto.addEventListener(
	"change",
	function (event) {
		if ($auto.checked) {
			if (timer !== null) return;
			timer = setInterval(load, 20000);
		}
		else {
			if (timer === null) return;
			clearInterval(timer);
			timer = null;
		}
	}
);

load();
