import "./plotly.min.js";

var records = new Array();

function $T(string) {
	return document.createTextNode(string);
}

function $E(name, attributes, children) {
	var element = document.createElementNS(document.documentElement.namespaceURI, name);
	if (attributes != null)
		for (var name in attributes)
			element.setAttribute(name, attributes[name]);
	if (Array.isArray(children))
		children.forEach(
			function (child) {
				return element.appendChild(child);
			}
		);
	return element;
}

function string_from_Date(value, seperator = " ") {
	var date = new Date(value);
	return (
		date.getFullYear().toString()
			+ "-"
			+ (date.getMonth() + 1).toString().padStart(2, "0")
			+ "-"
			+ date.getDate().toString().padStart(2, "0")
			+ seperator
			+ date.getHours().toString().padStart(2, "0")
			+ ":"
			+ date.getMinutes().toString().padStart(2, "0")
			+ ":"
			+ date.getSeconds().toString().padStart(2, "0")
	);
}

document.body.style["margin"] = "1ex";

var $dashboard;
document.body.appendChild(
	$dashboard = $E("div", {"id": "dashboard"})
);

function show_dashboard(row) {
	$dashboard.textContent = "";
	if (row == null)
		$dashboard.appendChild(
			$E("div", {"class": "nodata"}, [
				$T("No data")
			])
		);
	else {
		$dashboard.appendChild(
			$E("div", {"class": "title"},
				row[0].split("T").map(
					function (s) {
						return $E("span", null, [$T(s)]);
					}
				)
			)
		);
		function $item(title, index, unit) {
			return $E("div", {"class": "item"}, [
				$E("span", {"class": "name"}, [
					$T(title)
				]),
				$E("span", {"class": "value"}, [
					$T(row[index]),
					$E("span", null, [$T(unit)])
				])
			])
		}
		$dashboard.appendChild(
			$E("div", {"class": "items"}, [
				$item("Temperature", 1, "\u2103"),
				$item("Pressure", 2, "Pa"),
				$item("Humidity", 3, "%")
			])
		);
	}
}

show_dashboard(null);

var $save_GPS;
document.body.appendChild(
	$E("p", {"class": "download-links"}, [
		$E("a", {"href": "setting.html"}, [
			$T("Settings")
		]),
		$E("a", {"href": "recent.csv", "download": ""}, [
			$T("Download recent data")
		]),
		$E("a", {"href": "all.csv", "download": ""}, [
			$T("Download all data")
		]),
		$save_GPS = $E("a", {"href": "#"}, [
			$T("Save GPS data")
		])
	])
);

var $reflesh, $auto;
document.body.appendChild(
	$reflesh = $E("form", {"class": "reflesh"}, [
		$auto = $E("input", {"type": "checkbox"}),
		$E("label", null, [$T("Auto reflesh")]),
		$E("button", {"type": "submit"}, [$T("Reflesh now")])
	])
);

var $list;
document.body.appendChild(
	$E("table", null, [
		$E("thead", null, [
			$E("tr", null, [
				$E("th", null, [$T("Time")]),
				$E("th", null, [$T("Temperature")]),
				$E("th", null, [$T("Pressure")]),
				$E("th", null, [$T("Humidity")])
			])
		]),
		$list = $E("tbody")
	])
);

var $plot_temperature, $plot_pressure, $plot_humidity;
document.body.appendChild(
	$plot_temperature = $E("div", {"class": "plot"})
);
document.body.appendChild(
	$plot_pressure = $E("div", {"class": "plot"})
);
document.body.appendChild(
	$plot_humidity = $E("div", {"class": "plot"})
);

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
			margin: {r: 8}
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

var $loading = $E("h1", null, [$T("Loading...")]);
$loading.hidden = true;
document.body.appendChild($loading);

function load() {
	$plot_temperature.hidden = true;
	$plot_pressure.hidden = true;
	$plot_humidity.hidden = true;
	$list.textContent = null;
	$loading.hidden = false;
	var xhr = new XMLHttpRequest();
	xhr.onloadend = function (event) {
		$loading.hidden = true;
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
				$E("tr", null,
					fields.map(
						function (field) {
							return $E("td", null, [$T(field)]);
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

var GPS = new Array;

var $GPS;
document.body.appendChild(
	$E("table", null, [
		$E("thead", null, [
			$E("tr", null, [
				$E("th", null, [$T("Time")]),
				$E("th", null, [$T("Latitude")]),
				$E("th", null, [$T("Longitude")]),
				$E("th", null, [$T("Altitude")])
			])
		]),
		$GPS = $E("tbody")
	])
);

if (!("geolocation" in navigator)) {
	document.body.appendChild($E("p", null, [$T("Geo-location is not support in this browser")]));
}
else {
	var $plot_GPS = $E("div", {"class": "plot"});
	$plot_GPS.hidden = true;
	document.body.appendChild($plot_GPS);

	function plot_GPS() {
			$plot_GPS.hidden = false;
			Plotly.react(
			$plot_GPS,
			{
				data: [
					{
						type: "scatter",
						mode: "lines+markers",
						marker: {color: "red"},
						x: GPS.map(function (record) {return record[2];}),
						y: GPS.map(function (record) {return record[1];})
					}
				],
				layout: {
					title: "Position",
					xaxis: {
						title: "Longitude (E)",
						range: [113.836945, 114.331024]
					},
					yaxis: {
						title: "Latitude (N)",
						range: [22.214888, 22.539684],
						scaleanchor: "x"
					},
					margin: {
						r: 8
					},
					images: [
						{
							source: "HK.jpg",
							layer: "below",
							xref: "x",
							yref: "y",
							x: 113.8331682,
							y: 22.14320451,
							sizex: 0.57957280,
							sizey: 0.41995108,
							xanchor: "left",
							yanchor: "bottom",
							sizing: "stretch",
							opacity: 0.5
						}
					]
				},
				config: {
					responsive: true
				}
			}
		);
	}

	function record_GPS(spacetime) {
		if (spacetime === null || typeof spacetime === "undefined") return;
		var coords = spacetime.coords;
		GPS.push(
			[
				string_from_Date(spacetime.timestamp, "T"),
				coords.latitude, coords.longitude, coords.altitude,
				coords.accuracy, coords.altitudeAccuracy,
				coords.heading, coords.speed
			]
		);
		var $tr = $E("tr", null,
			[string_from_Date(spacetime.timestamp), coords.latitude, coords.longitude, coords.altitude].map(
				function add_td(value) {
					return $E("td", null, value == null ? null : [$T(String(value))]);
				}
			)
		);
		if ($GPS.firstChild)
			$GPS.insertBefore($tr, $GPS.firstChild);
		else
			$GPS.appendChild($tr);
		plot_GPS();
	}

	function get_GPS() {
		navigator.geolocation.getCurrentPosition(
			record_GPS,
			function (error) {
				console.error("GeoLocationError: ", error.message);
			},
			{timeout: 15000, enableHighAccuracy: true}
		)
	}

	get_GPS();
	// setInterval(get_GPS, 30000);
	navigator.geolocation.watchPosition(record_GPS);
}

var $GPS_downloader = $E("a", {"download": "gps.csv"});
$GPS_downloader.hidden = true;
document.body.appendChild($GPS_downloader);
function save_GPS() {
	var content =
		"Time,Latitude,Longitude,Altitude,Horizontal accuracy,Vertical accuracy,Heading,Speed\r\n"
			+ GPS.map(function (record) {return record.join(",");}).join("\r\n");
	var oldobj = $GPS_downloader.getAttribute("href");
	if (oldobj) URL.revokeObjectURL(objurl);
	$GPS_downloader.setAttribute("href", URL.createObjectURL(new Blob(Array.of(content), {type: "text/csv"})));
	setTimeout(function () {$GPS_downloader.click();}, 1000);
}

$save_GPS.addEventListener(
	"click",
	function (event) {
		event.preventDefault();
		save_GPS();
	}
);
