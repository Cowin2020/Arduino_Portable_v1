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

function Timer() {
	this.interval = null;
	this.update();
}
Timer.prototype = {
	run() {},
	update() {},
	set(checked) {
		if (checked) {
			if (this.interval !== null) return;
			this.interval = setInterval(this.run.bind(this), 30000);
		}
		else {
			if (this.interval === null) return;
			clearInterval(this.interval);
			this.interval = null;
		}
	}
};

document.body.style["margin"] = "1ex";

/* Dashboard */

var $dashboard = new Object;
$dashboard.items = new Array(data_fields.length);
$dashboard.root = $E("div", {"id": "dashboard"}, [
	$dashboard.nodata = $E("div", {"id": "nodata"}, [
		$T("No data")
	]),
	$dashboard.datetime = $E("div", {"id": "datetime"}, [
		$dashboard.date = $E("span"),
		$dashboard.time = $E("span")
	]),
	$E("div", {"id": "items"},
		data_fields.map(
			function (field, index) {
				return $E("div", {"class": "item"}, [
					$E("span", {"class": "name"}, [$T(field.name)]),
					$E("span", {"class": "value"}, [
						$dashboard.items[index] = $E("span", {"class": "value"}),
						$E("span", {"class": "unit"}, [$T(field.unit)])
					])
				]);
			}
		)
	)
]);
$dashboard.nodata.hidden = true;
$dashboard.datetime.hidden = true;
$dashboard.items.hidden = true;
document.body.appendChild($dashboard.root);

function show_dashboard(row) {
	if (row == null) {
		$dashboard.nodata.hidden = false;
		$dashboard.datetime.hidden = true;
		$dashboard.items.hidden = true;
	}
	else {
		$dashboard.nodata.hidden = true;
		$dashboard.datetime.hidden = false;
		$dashboard.items.hidden = false;
		var date_time = row[0].split("T");
		$dashboard.date.textContent = date_time[0];
		$dashboard.time.textContent = date_time[1];
		data_fields.forEach(
			function (field, index) {
				$dashboard.items[index].textContent = row[field.index];
			}
		);
	}
}

show_dashboard(null);

/* Download links */

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

/* Refresh */

var $refresh, $auto_refresh, $report, $auto_report;
document.body.appendChild(
	$E("p", {"class": "timers"}, [
		$refresh = $E("form", {"class": "refresh"}, [
			$E("label", null, [
				$auto_refresh = $E("input", {"type": "checkbox", "checked": ""}),
				$T("Auto refresh")
			]),
			$E("button", {"type": "submit"}, [$T("Refresh now")])
		]),
		$report = $E("form", {"class": "report"}, [
			$E("label", null, [
				$auto_report = $E("input", {"type": "checkbox", "checked": ""}),
				$T("Auto report")
			]),
			$E("button", {"type": "submit"}, [$T("Report now")])
		])
	])
);

/* Sensor data */

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
		$plot.hidden = true;
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
			dragmode: false,
			margin: {r: 8}
		};
		var config = {
			responsive: true
		};
		data_fields.forEach(
			function (field, index) {
				Plotly.react(
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

$refresh.addEventListener(
	"submit",
	function (event) {
		event.preventDefault();
		load();
	}
);

function RefreshTimer() {
	Timer.call(this);
}
RefreshTimer.prototype = {
	__proto__: Timer.prototype,
	run: load,
	update() {
		this.set($auto_refresh.checked);
	}
};

var refresh_timer = new RefreshTimer();
$auto_refresh.addEventListener("change", refresh_timer.update.bind(refresh_timer));
load();

/* GPS */

var GPS = new Array;
var $GPS = new Object;

$GPS.table = $E("table", null, [
	$E("tbody", null, [
		$E("tr", null, [
			$E("th", null, [$T("Time")]),
			$GPS.time = $E("td")
		]),
		$E("tr", null, [
			$E("th", null, [$T("Latitude")]),
			$GPS.latitude = $E("td")
		]),
		$E("tr", null, [
			$E("th", null, [$T("Longitude")]),
			$GPS.longitude = $E("td")
		]),
		$E("tr", null, [
			$E("th", null, [$T("Altitude")]),
			$GPS.altitude = $E("td")
		])
	])
]);
$GPS.table.hidden = true;
document.body.appendChild($GPS.table);

$GPS.plot = $E("div", {"class": "plot"});
$GPS.plot.hidden = true;
document.body.appendChild($GPS.plot);

if (!("geolocation" in navigator)) {
	document.body.appendChild($E("p", null, [$T("Geo-location is not support in this browser")]));
}
else {
	function plot_GPS() {
		$GPS.plot.hidden = false;
		Plotly.react(
			$GPS.plot,
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
					],
					dragmode: false,
					margin: {
						r: 8
					}
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
		$GPS.time.textContent = string_from_Date(spacetime.timestamp);
		$GPS.latitude.textContent = coords.latitude;
		$GPS.longitude.textContent = coords.longitude;
		$GPS.altitude.textContent = coords.altitude;
		$GPS.table.hidden = false;
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
	setInterval(get_GPS, 30000);
	// navigator.geolocation.watchPosition(record_GPS);
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
	if (Array.isArray(position))
		for (var i = 1; Alone.data_fields.length > i; ++i)
			formdata.append(Alone.data_fields[i], position[i]);
	fetch(Alone.report, {method: 'POST', body: formdata})
		.catch(function () {});
}

$report.addEventListener(
	"submit",
	function (event) {
		event.preventDefault();
		report();
	}
);

function ReportTimer() {
	Timer.call(this);
}
ReportTimer.prototype = {
	__proto__: Timer.prototype,
	run: report,
	update() {
		this.set($auto_report.checked);
	}
};

var report_timer = new ReportTimer();
$auto_report.addEventListener("change", report_timer.update.bind(report_timer));

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

/* ************************************************************************* */
