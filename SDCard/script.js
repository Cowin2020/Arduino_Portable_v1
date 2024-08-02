import "./plotly.min.js";

var start_delay = 3000; /* milliseconds */

/* Utilities */

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

function string_from_Date(date, seperator = " ") {
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
	set(enabled) {
		if (enabled) {
			if (this.interval !== null) return;
			this.interval = setInterval(this.run.bind(this), Alone.measure_interval);
		}
		else {
			if (this.interval === null) return;
			clearInterval(this.interval);
			this.interval = null;
		}
	}
};

/* Page structure */

document.body.style["margin"] = "1ex";

var $dashboard = new Object;
$dashboard.items = new Array(Alone.data_fields.length - 1);
$dashboard.root = $E("div", {"id": "dashboard"}, [
	$dashboard.nodata = $E("div", {"id": "nodata"}, [
		$T("No data")
	]),
	$dashboard.datetime = $E("div", {"id": "datetime"}, [
		$dashboard.date = $E("span"),
		$dashboard.time = $E("span")
	]),
	$E("div", {"id": "items"},
		Alone.data_fields.slice(1).map(
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
		for (var i = 1; i < Alone.data_fields.length; ++i) {
			$dashboard.items[i - 1].textContent = row[i];
		}
	}
}

show_dashboard(null);

document.body.appendChild(
	$E("p", {"class": "download-links"},
		(function (children) {
			if (Alone.operator) {
				children.push(
					$E("a", {"href": "setting.html"}, [
						$T("Settings")
					])
				);
			}
			children.push(
				$E("a", {"href": "data/recent.csv", "download": "data_recent.csv"}, [
					$T("Recent weather data")
				])
			);
			children.push(
				$E("a", {"href": Alone.data_file, "download": ""}, [
					$T("All weather data")
				])
			);
			children.push(
				$E("a", {"href": "gps/recent.csv", "download": "gps_recent.csv"}, [
					$T("Recent GPS data")
				])
			);
			children.push(
				$E("a", {"href": "gps.csv", "download": ""}, [
					$T("All GPS data")
				])
			);
			return children;
		}(new Array))
	)
);

var $refresh, $auto_refresh, $auto_report;
$refresh = $E("form", {"class": "refresh"}, [
	$E("label", null, [
		$auto_refresh = $E("input", {"type": "checkbox", "checked": ""}),
		$T("Auto refresh")
	]),
	$E("button", {"type": "submit"}, [$T("Refresh now")])
]);
void function () {
	var $forms = [$refresh];
	if (Alone.operator)
		$forms.push(
			$E("p", {"class": "timers"}, [
				$refresh,
				$E("label", {"class": "report"}, [
					$auto_report = $E("input", {"type": "checkbox", "checked": ""}),
					$T("Auto report")
				])
			])
		);
	document.body.appendChild($E("p", {"class": "timers"}, $forms))
}();

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

var $plots = Alone.data_fields.slice(1).map(
	function () {
		var $plot = $E("div", {"class": "plot"});
		$plot.hidden = true;
		document.body.appendChild($plot);
		return $plot;
	}
);

var $data_loading = $E("h1", null, [$T("Loading...")]);
$data_loading.hidden = true;
document.body.appendChild($data_loading);

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

/* Actions */

function hide_plots() {
	$plots.forEach(function ($plot) {$plot.hidden = true;});
}

function data_plots(rows) {
	if (!Array.isArray(rows))
		hide_plots();
	else {
		function column(n) {
			return rows.map(function (row) {return row[n]});
		}
		var time = column(0);
		var layout = {
			uirevision: true,
			dragmode: false,
			margin: {r: 8}
		};
		var config = {
			responsive: true
		};
		Alone.data_fields.slice(1).forEach(
			function (field, index) {
				$plots[index].hidden = false;
				Plotly.react(
					$plots[index],
					{
						data: [{x: time, y: column(index + 1)}],
						layout: {title: field.name + " (" + field.unit + ")", ...layout},
						config: config
					}
				);
			}
		);
	}
}

function data_load() {
	return new Promise(
		function (resolve, reject) {
			hide_plots();
			$list.textContent = null;
			$data_loading.hidden = false;
			var xhr = new XMLHttpRequest();
			xhr.onloadend = function (event) {
				$data_loading.hidden = true;
				var text = xhr.responseText;
				if (text == null || xhr.status !== 200) {
					alert("Failed to load data");
					return reject(xhr);
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
					data_plots(null);
					return resolve();
				}
				show_dashboard(rows[rows.length - 1]);
				data_plots(rows);
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
				return resolve();
			};
			xhr.open("GET", "data/recent.csv", true);
			xhr.send(null);
		}
	);
}

var GPS = new Array;

function GPS_show() {
	if (!GPS.length) {
		$GPS.table.hidden = true;
		$GPS.plot.hidden = true;
		return;
	}

	var last = GPS[GPS.length - 1];
	$GPS.time.textContent = string_from_Date(last[0]);
	$GPS.latitude.textContent = last[1];
	$GPS.longitude.textContent = last[2];
	$GPS.altitude.textContent = last[3];
	$GPS.table.hidden = false;

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
					title: "Longitude (E)"
				},
				yaxis: {
					title: "Latitude (N)",
					scaleanchor: "x"
				},
				images: [
					{
						source: "HK.jpg",
						layer: "below",
						xref: "x",
						yref: "y",
						x: 113.8303978,
						y: 22.1501391,
						sizex: 0.61396362,
						sizey: 0.41495771,
						xanchor: "left",
						yanchor: "bottom",
						sizing: "stretch",
						opacity: 0.75
					}
				],
				uirevision: true,
				dragmode: false,
				margin: {
					r: 8
				}
			},
			config: {
				responsive: true,
				scrollZoom: true
			}
		}
	);
}

function GPS_load() {
	return new Promise(
		function (resolve, reject) {
			var xhr = new XMLHttpRequest();
			xhr.onloadend = function (event) {
				var text = xhr.responseText;
				if (text == null || xhr.status !== 200) {
					alert("Failed to load GPS records");
					return reject(xhr);
				}
				var lines = text.split("\r\n");
				if (!lines || !(lines.length > 0)) return;
				var records = new Array;
				for (var i = 1; i < lines.length; ++i) {
					var line = lines[i].trim();
					if (!line || typeof line !== "string") continue;
					var fields = line.split(",");
					var record = new Array;
					if (fields.length > 0) {
						record.push(new Date(fields[0]));
						for (var j = 1; j < fields.length; ++j)
							record.push(Number.parseFloat(fields[j]));
					}
					records.push(record);
				}
				GPS = records;
				GPS_show();
				return resolve();
			};
			xhr.open("GET", "gps/recent.csv", true);
			xhr.send(null);
		}
	);
}

function load_all() {
	return (
		data_load()
			.catch(function () {})
			.then(function () {return GPS_load();})
			.catch(function () {})
	);
}

$refresh.addEventListener(
	"submit",
	function (event) {
		event.preventDefault();
		return load_all();
	}
);

function RefreshTimer() {
	Timer.call(this);
}
RefreshTimer.prototype = {
	__proto__: Timer.prototype,
	run: load_all,
	update() {
		this.set($auto_refresh.checked);
	}
};

var refresh_timer = new RefreshTimer();
$auto_refresh.addEventListener("change", refresh_timer.update.bind(refresh_timer));
setTimeout(load_all, start_delay);

if (Alone.operator) {
	if (!("geolocation" in navigator))
		document.body.appendChild($E("p", null, [$T("Geo-location is not support in this browser")]));
	else {
		function make_body(timestamp, coords) {
			var body = new URLSearchParams;
			body.append("identity",  Alone.identity);
			body.append("time",      timestamp);
			body.append("latitude",  coords.latitude);
			body.append("longitude", coords.longitude);
			body.append("altitude",  coords.altitude);
			return body;
		}
		function GPS_upload(timestamp, coords) {
			var body = make_body(timestamp, coords);
			fetch("/gps/upload.exe", {method: "POST", body: body})
				.catch(function () {});
		}
		function GPS_report(timestamp, coords) {
			var body = make_body(timestamp, coords);
			fetch(Alone.report, {method: "POST", body: body})
				.catch(function () {});
		}

		function GPS_record(spacetime) {
			if (spacetime === null || typeof spacetime === "undefined") return;
			var time = new Date(spacetime.timestamp);
			var timestamp = string_from_Date(time, "T");
			var coords = spacetime.coords;
			GPS.push([time, coords.latitude, coords.longitude, coords.altitude]);
			GPS_show();
			GPS_upload(timestamp, coords);
			if ($auto_report.checked)
				GPS_report(timestamp, coords);
		}

		function GPS_request() {
			navigator.geolocation.getCurrentPosition(
				GPS_record,
				function (error) {
					console.error("GeoLocationError: ", error.message);
				},
				{timeout: 15000, enableHighAccuracy: true}
			)
		}
		setTimeout(GPS_request, start_delay);
		setInterval(GPS_request, Alone.measure_interval);
		// navigator.geolocation.watchPosition(GPS_record);
	}
}

/* ************************************************************************* */
