import "./plotly.min.js";

var data_fields = [
	/* "Time" is excluded */
	{
		index: 1,
		name: "Temperature",
		unit: "\u2103"
	},
	{
		index: 2,
		name: "Pressure",
		unit: "Pa"
	},
	{
		index: 3,
		name: "Humidity",
		unit: "%"
	}
];

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
		$dashboard.appendChild(
			$E("div", {"class": "items"},
				data_fields.map(
					function (field) {
						return $E("div", {"class": "item"}, [
							$E("span", {"class": "name"}, [
								$T(field.name)
							]),
							$E("span", {"class": "value"}, [
								$T(row[field.index]),
								$E("span", null, [$T(field.unit)])
							])
						]);
					}
				)
			)
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

var $refresh, $auto_refresh, $report, $auto_report;
document.body.appendChild(
	$E("p", {"class": "timers"}, [
		$refresh = $E("form", {"class": "refresh"}, [
			$auto_refresh = $E("input", {"type": "checkbox"}),
			$E("label", null, [$T("Auto refresh")]),
			$E("button", {"type": "submit"}, [$T("Refresh now")])
		]),
		$report = $E("form", {"class": "report"}, [
			$auto_report = $E("input", {"type": "checkbox"}),
			$E("label", null, [$T("Auto report")]),
			$E("button", {"type": "submit"}, [$T("Report now")])
		])
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

var $plots = data_fields.map(
	function () {
		var $plot = $E("div", {"class": "plot"});
		document.body.appendChild($plot);
		return $plot;
	}
);

function hide_plot() {
	$plots.forEach(function ($plot) {$plot.hidden = true;});
}

function show_plot(rows) {
	if (!Array.isArray(rows))
		hide_plot();
	else {
		$plots.forEach(function ($plot) {$plot.hidden = false;});
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
		data_fields.forEach(
			function (field, index) {
				Plotly.newPlot(
					$plots[index],
					{
						data: [{x: time, y: column(field.index)}],
						layout: {title: field.name + " (" + field.unit + ")", ...layout},
						config: config
					}
				);
			}
		);
	}
}

var $loading = $E("h1", null, [$T("Loading...")]);
$loading.hidden = true;
document.body.appendChild($loading);

function load() {
	hide_plot();
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

function report() {
	if (!GPS.length) return;
	var position = GPS[GPS.length - 1];
	var formdata = new FormData;
	formdata.append('identity',  Alone.identity);
	formdata.append('time',      position[0]);
	formdata.append('latitude',  position[1]);
	formdata.append('longitude', position[2]);
	formdata.append('latitude',  position[3]);
	if (Array.isArray(latest))
		for (var i = 1; Alone.data_fields.length > i; ++i)
			formdata.append(Alone.data_fields[i], latest[i]);
	fetch(Alone.report, {method: 'POST', body: formdata})
		.catch(function () {});
}

$refresh.addEventListener(
	"submit",
	function (event) {
		event.preventDefault();
		load();
	}
);

var refresh_timer = null;

$auto_refresh.addEventListener(
	"change",
	function (event) {
		if ($auto_refresh.checked) {
			if (refresh_timer !== null) return;
			refresh_timer = setInterval(load, 20000);
		}
		else {
			if (refresh_timer === null) return;
			clearInterval(refresh_timer);
			refresh_timer = null;
		}
	}
);

$report.addEventListener(
	"submit",
	function (event) {
		event.preventDefault();
		report();
	}
);

var report_timer = null;

$auto_report.addEventListener(
	"change",
	function (event) {
		if ($auto_report.checked) {
			if (report_timer !== null) return;
			report_timer = setInterval(report, 20000);
		}
		else {
			if (report_timer === null) return;
			clearInterval(report_timer);
			report_timer = null;
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
