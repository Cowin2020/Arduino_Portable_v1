import "./plotly.min.js";

/* Options */

var GPS_watch = false;

/* Constants */

var data_recent = "data/recent.csv";
var gps_recent = "gps/recent.csv";

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

function string_from_Date(value, seperator = " ") {
	var date = new Date(value);
	return (
		new Date(date).getFullYear().toString()
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

var MILLISECONDS_FROM_1970_TO_2000 = 946684800000; /* = Date.UTC(2000, 0, 1, 0, 0, 0, 0) */

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
			this.interval = setInterval(this.run.bind(this), Application.measure_interval);
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

document.body.appendChild(
	$E("p", null, [
		$T("Camp: "), $T(Application.campaign),
		$T(" | Organisation: "), $T(Application.organisation),
		$T(" | Device: "), $T(Application.device)
	])
);

function Dashboard() {
	this.data = new Array(Application.data_fields.length);
	this.$item_list = new Array(Application.data_fields.length);
	this.$root = $E("div", {"class": "dashboard"}, [
		this.$nodata = $E("div", {"class": "nodata"}, [
			$T("No data")
		]),
		this.$datetime = $E("div", {"class": "datetime"}, [
			this.$date = $E("span"),
			this.$time = $E("span")
		]),
		this.$items = $E("div", {"class": "items"},
			Application.data_fields
			.map(
				function (field, index) {
					if (field.title == null)
						return this.$item_list[index] = null;
					else
						return $E("div", {"class": "item"}, [
							$E("span", {"class": "name"}, [$T(field.title)]),
							$E("span", {"class": "value"}, [
								this.$item_list[index] = $E("span", {"class": "value"}),
								$E("span", {"class": "unit"}, [$T(field.unit ? field.unit : "")])
							])
						]);
				},
				this
			)
			.filter(
				function (element) {
					return element != null;
				}
			)
		)
	]);
	this.$nodata.hidden = true;
	this.$datetime.hidden = true;
	this.$items.hidden = true;
	return this;
}
Dashboard.prototype = {
	show(row) {
		if (row == null) {
			this.$nodata.hidden = false;
			this.$datetime.hidden = true;
			this.$items.hidden = true;
		}
		else {
			this.$nodata.hidden = true;
			this.$datetime.hidden = false;
			this.$items.hidden = false;
			var date_time = row[0].split("T");
			this.$date.textContent = date_time[0];
			this.$time.textContent = date_time[1];
			this.$item_list.forEach(
				function ($item, index) {
					if (this.$item != null)
						this.$item.textContent = this.data[index] = row[index];
				},
				this
			);
		}
	}
};

var dashboard = new Dashboard;
document.body.appendChild(dashboard.$root);

dashboard.show(null);

document.body.appendChild(
	$E("p", {"class": "download-links"},
		(function (children) {
			if (Application.operator) {
				children.push(
					$E("a", {"href": "setting.html"}, [
						$T("Settings")
					])
				);
			}
			children.push(
				$E("a", {"href": data_recent, "download": "data_recent.csv"}, [
					$T("Recent weather data")
				])
			);
			children.push(
				$E("a", {"href": Application.data_file, "download": ""}, [
					$T("All weather data")
				])
			);
			children.push(
				$E("a", {"href": gps_recent, "download": "gps_recent.csv"}, [
					$T("Recent GPS data")
				])
			);
			children.push(
				$E("a", {"href": Application.gps_file, "download": ""}, [
					$T("All GPS data")
				])
			);
			return children;
		}(new Array))
	)
);

var $refresh, $auto_refresh, $auto_report, $upload;
$refresh = $E("form", {"class": "refresh"}, [
	$E("label", null, [
		$auto_refresh = $E("input", {"type": "checkbox", "checked": ""}),
		$T("Auto refresh")
	]),
	$E("button", {"type": "submit"}, [$T("Refresh now")])
]);
void function () {
	var $forms = [$refresh];
	if (Application.operator)
		$forms.push(
			$E("p", {"class": "timers"}, [
				$refresh,
				$E("label", {"class": "report"}, [
					$auto_report = $E("input", {"type": "checkbox", "checked": ""}),
					$T("Report position")
				]),
				$upload = $E("button", {"type": "button"}, [
					$T("Upload data")
				])
			])
		);
	document.body.appendChild($E("p", {"class": "timers"}, $forms))
}();

var $list;
document.body.appendChild(
	$E("table", null, [
		$E("caption", null, [$T("Weather data")]),
		$E("thead", null, [
			$E("tr", null,
				[$E("th", null, [$T("time")])].concat(
					Application.data_fields
					.map(
						function (field) {
							if (field.title == null) return null;
							var title = field.title;
							if (field.unit != null)
								title = title + " (" + field.unit + ")";
							return $E("th", null, [$T(title)]);
						}
					)
					.filter(
						function ($th) {
							return $th != null;
						}
					)
				)
			)
		]),
		$list = $E("tbody")
	])
);
var $plots = Application.data_fields.map(
	function (field) {
		if (field.title == null) return null;
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
	$E("caption", null, [$T("Last GPS position")]),
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
	$plots.forEach(
		function ($plot) {
			if ($plot != null)
				$plot.hidden = true;
		}
	);
}

function data_plots(rows) {
	if (!Array.isArray(rows))
		hide_plots();
	else {
		var time = rows.map(function (row) {return new Date(row[0]);});
		var layout = {
			uirevision: true,
			dragmode: false,
			margin: {r: 8}
		};
		var config = {
			responsive: true
		};
		for (var i = 0; Application.data_fields.length > i; ++i) {
			var field = Application.data_fields[i];
			var $plot = $plots[i];
			if (field.title == null || $plot == null) continue;
			$plot.hidden = false;
			var title = field.title;
			if (field.unit != null)
				title = title + " (" + field.unit + ")";
			Plotly.react(
				$plot,
				{
					data: [{x: time, y: rows.map(function (row) {return row[i];})}],
					layout: {title: title},
					config: config
				}
			);
		}
	}
}

function data_load() {
	hide_plots();
	$list.textContent = null;
	$data_loading.hidden = false;
	return (
		fetch("data/recent.csv")
		.then(
			function (response) {
				$data_loading.hidden = true;
				if (response.status !== 200) {
					alert("Failed to load data");
					return Promise.reject(response.status);
				}
				return response.text();
			}
		)
		.then(
			function (text) {
				var rows = text.split("\n").map(
					function (line) {
						return line.trim().split(",");
					}
				);
				rows.shift();
				while (rows.length && rows[rows.length - 1].length < Application.data_fields.length)
					rows.pop();
				if (!(rows.length > 0)) {
					dashboard.show(null);
					data_plots(null);
					return Promise.resolve();
				}
				dashboard.show(rows[rows.length - 1]);
				data_plots(rows);
				for (var i = rows.length; --i >= 0; ) {
					var row = rows[i];
					$list.appendChild(
						$E("tr", null,
							[$E("td", null, [$T(row[0])])]
							.concat(
								row.map(
									function (field, j) {
										if (Application.data_fields[j].title == null) return null;
										return $E("td", null, [$T(field)]);
									}
								)
							)
							.filter(
								function ($td) {
									return $td != null;
								}
							)
						)
					);
				}
				return Promise.resolve();
			}
		)
	);
}

var GPS = new Array;

var GPS_index_latitude = Application.gps_fields.findIndex(function (field) {return field.name === "latitude";});
var GPS_index_longitude = Application.gps_fields.findIndex(function (field) {return field.name === "longitude";});
var GPS_index_altitude = Application.gps_fields.findIndex(function (field) {return field.name === "altitude";});

if ("L" in window) {
	/* initialize leaflet */
	$GPS.plot.hidden = false;
	$GPS.map =
		window.L.map(
			$GPS.plot,
			{
				center: L.latLng(22.35, 114.130),
				zoom: 12
			}
		);
	$GPS.map.addLayer(
		new L.TileLayer(
			"https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png",
			{
				attribution: "Map data &#xA9; <a href='https://www.openstreetmap.org/about/'>OpenStreetMap</a>"
			}
		)
	);
	$GPS.plot.hidden = true;
}

function GPS_plot() {
	if ("L" in window) {
		/* use leaflet */
		$GPS.plot.hidden = false;
		if ("polyline" in $GPS) {
			$GPS.polyline.remove();
			delete $GPS.polyline;
		}
		$GPS.polyline =
			window.L.polyline(
				GPS.map(
					function (record) {
						return [record[GPS_index_latitude], record[GPS_index_longitude]];
					}
				),
				{
					color: "orangered"
				}
			);
		$GPS.map.addLayer($GPS.polyline);
	}
	else {
		/* use plotly */
		$GPS.plot.hidden = false;
		Plotly.react(
			$GPS.plot,
			{
				data: [
					{
						type: "scatter",
						mode: "lines+markers",
						marker: {color: "red"},
						x: GPS.map(function (record) {return record[GPS_index_longitude];}),
						y: GPS.map(function (record) {return record[GPS_index_latitude];})
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
}

function GPS_show() {
	if (!GPS.length) {
		$GPS.table.hidden = true;
		$GPS.plot.hidden = true;
		return;
	}

	var last = GPS[GPS.length - 1];
	$GPS.time.textContent = string_from_Date(last[2]);
	$GPS.latitude.textContent = last[GPS_index_latitude];
	$GPS.longitude.textContent = last[GPS_index_longitude];
	$GPS.altitude.textContent = last[GPS_index_altitude];
	$GPS.table.hidden = false;

	GPS_plot();
}

function GPS_load() {
	return (
		fetch("gps/recent.csv")
		.then(
			function (response) {
				$data_loading.hidden = true;
				if (response.status !== 200) {
					alert("Failed to load GPS records");
					return Promise.reject(response.status);
				}
				return response.text();
			}
		)
		.then(
			function (text) {
				var lines = text.split("\r\n");
				if (!lines || !(lines.length > 0)) return;
				var records = new Array;
				for (var i = 1; lines.length > i; ++i) {
					var line = lines[i].trim();
					if (!line || typeof line !== "string") continue;
					var fields = line.split(",");
					var record = new Array;
					for (var j = 0; fields.length > j; ++j)
						if (Application.gps_fields[j].unit)
							record.push(Number.parseFloat(fields[j]));
						else
							record.push(fields[j]);
					records.push(record);
				}
				GPS = records;
				GPS_show();
				return Promise.resolve();
			}
		)
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
load_all();

if (Application.operator) {
	if (!("geolocation" in navigator))
		document.body.appendChild($E("p", null, [$T("Geo-location is not support in this browser")]));
	else {
		function GPS_upload(planned_time, browser_time, position_time, coords) {
			var body = new URLSearchParams;
			body.append("campaign",     Application.campaign);
			body.append("organisation", Application.organisation);
			body.append("device",       Application.device);
			body.append("time",          planned_time);
			body.append("browser_time",  browser_time);
			body.append("position_time", position_time);
			body.append("latitude",     coords.latitude);
			body.append("longitude",    coords.longitude);
			body.append("altitude",     coords.altitude);
			fetch("/gps/upload.exe", {method: "POST", body: body})
				.catch(function () {});
		}
		function GPS_report(timestamp, coords) {
			var body = new URLSearchParams;
			body.append("campaign",     Application.campaign);
			body.append("organisation", Application.organisation);
			body.append("device",       Application.device);
			body.append("time",         timestamp);
			body.append("latitude",     coords.latitude);
			body.append("longitude",    coords.longitude);
			body.append("altitude",     coords.altitude);
			for (var i = 0; dashboard.data.length > i; ++i)
				if (dashboard.data[i] != null)
					body.append(Application.data_fields[i].name, dashboard.data[i]);
			fetch(Application.monitor_URL, {method: "POST", body: body})
				.catch(function () {});
		}

		function GPS_record(planned_time, spacetime) {
			if (spacetime === null || typeof spacetime === "undefined") return;
			var browser_time = string_from_Date(Date.now(), "T");
			var position_time = string_from_Date(spacetime.timestamp, "T");
			var coords = spacetime.coords;
			GPS.push([planned_time, browser_time, position_time, coords.latitude, coords.longitude, coords.altitude]);
			GPS_show();
			GPS_upload(planned_time, browser_time, position_time, coords);
			if ($auto_report.checked)
				GPS_report(position_time, coords);
		}
		function GPS_error(error) {
			console.error("GeoLocationError: ", error.message);
		}
		var GPS_options = {
			timeout: Application.measure_interval / 4,
			enableHighAccuracy: true
		};
		function GPS_request() {
			var now_plus_half =
				Date.now()
					- MILLISECONDS_FROM_1970_TO_2000
					+ Application.measure_interval / 2;
			var planned_time =
				string_from_Date(
					now_plus_half
						- now_plus_half % Application.measure_interval
						+ MILLISECONDS_FROM_1970_TO_2000,
					"T"
				);
			navigator.geolocation.getCurrentPosition(GPS_record.bind(this, planned_time), GPS_error, GPS_options);
		}
		function GPS_start() {
			setInterval(GPS_request, Application.measure_interval);
		}
		setTimeout(GPS_start, (Date.now() - MILLISECONDS_FROM_1970_TO_2000) % Application.measure_interval);
		setTimeout(GPS_request, 0);
		function GPS_callback(spacetime) {
			GPS_record(string_from_Date(spacetime.timestamp), spacetime);
		}
		if (GPS_watch) navigator.geolocation.watchPosition(GPS_callback, GPS_error, GPS_options);
	}
	void function () {
		var subtle = null;
		if (window.isSecureContext) if ("crypto" in window) if ("subtle" in window.crypto) {
			subtle = window.crypto.subtle;
		}
		function authorization(query, body) {
			if (subtle == null)
				return Promise.resolve("BEARER " + Application.upload_password);
			var credential = Application.upload_username + query + body + Application.upload_username;
			var binary = new TextEncoder().encode(credential);
			return subtle.digest("SHA-256", binary).then(
				function (hash) {
					var digest = new Uint8Array(hash);
					var password = new TextEncoder().encode(Application.upload_password);
					var binary = new Uint8Array(password.length + 1 + digest.byteLength);
					binary.set(password, 0);
					binary[password.length] = 0x3A;  /* ASCII 3A = ':' */
					binary.set(digest, password.length + 1);
					var base64 = binary.toBase64();
					return Promise.resolve("BASIC " + base64);
				}
			);
		}
		function upload(site, device, body) {
			var params = new URLSearchParams();
			params.set("site", Application.campaign);
			params.set("device", Application.device);
			var query = params.toString();
			return authorization(query, body).then(
				function (auth) {
					var headers = new Headers();
					headers.set("AUTHORIZATION", auth);
					return fetch(
						Application.upload_URL + "?" + query,
						{
							method: "POST",
							headers: headers,
							body: body
						}
					);
				}
			);
		}
		$upload.addEventListener(
			"click",
			function (event) {
				event.preventDefault();
				fetch(Application.data_file)
				.then(
					function (response) {
						if (!response.ok)
							return Promise.reject("download weather data from device: " + response.status)
						return response.text();
					}
				)
				.then(
					function (text) {
						return upload(Application.campaign, Application.device, text);
					}
				)
				.then(
					function (response) {
						if (!response.ok)
							return Promise.reject("upload weather data to server: " + response.status)
					}
				)
				.then(
					function () {
						return fetch(Application.gps_file);
					}
				)
				.then(
					function (response) {
						if (!response.ok)
							return Promise.reject("download GPS data from device: " + response.status)
						return response.text();
					}
				)
				.then(
					function (text) {
						return upload(Application.campaign, Application.device, text);
					}
				)
				.then(
					function (response) {
						if (!response.ok)
							return Promise.reject("upload GPS data to server: " + response.status)
					}
				)
				.then(
					function () {
						alert("Success to upload data");
					},
					function (error) {
						alert("Failed to upload data: " + String(error));
					}
				);
			}
		);
	}();
	void function () {
		/* set device time */
		var body = new URLSearchParams();
		body.append("time", string_from_Date(Date.now(), "T"));
		fetch(
			"setting.exe",
			{
				method: "POST",
				body: body,
				redirect: "manual"
			}
		);
	}();
}

/* ************************************************************************* */
