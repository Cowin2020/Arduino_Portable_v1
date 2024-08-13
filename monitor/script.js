(function(p){document.readyState!=="loading"?p():document.addEventListener("DOMContentLoaded",p)})(function(p){"use strict";

var current_map = L.map(
	"map",
	{
		center: L.latLng(22.35, 114.130),
		zoom: 12,
		zoomSnap: 0.25,
		zoomDelta: 0.25
	}
)

current_map.addLayer(
	new L.TileLayer(
		"https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png",
		{
			attribution: "Map data &#xA9; <a href='https://www.openstreetmap.org/about/'>OpenStreetMap</a>"
		}
	)
);

current_map.addLayer(new L.LayerGroup());

var current_tooltips = new Array();

function current_clear() {
	current_tooltips.forEach(
		function (tooltip) {
			current_map.removeLayer(tooltip);
		}
	);
}

function current_plot(devices) {
	devices.forEach(
		function (device) {
			var tooltip = L.tooltip(
				L.latLng(device.latitude, device.longitude),
				{
					permanent: true,
					content: device.identity
				}
			);
			current_tooltips.push(tooltip);
			tooltip.openOn(current_map);
		}
	);
}

function current_load() {
	var xhr = new XMLHttpRequest();
	xhr.onloadend = function () {
		if (xhr.status !== 200 || xhr.responseText == null)
			return alert("Failed to load current position");
		try {
			var object = JSON.parse(xhr.responseText);
		}
		catch (e) {
			var object = null;
		}
		if (typeof object !== "object")
			return alert("Invalid format of current position file");
		var devices = new Array();
		for (var id in object) {
			var record = object[id];
			var latitude = Number.parseFloat(record[1]);
			var longitude = Number.parseFloat(record[2]);
			if (latitude != null && longitude != null)
				devices.push(
					{
						identity: id,
						time: record[0],
						latitude: latitude,
						longitude: longitude,
						altitude: Number.parseFloat(record[3])
					}
				);
		}
		current_clear();
		current_plot(devices);
	};
	xhr.open("GET", "current.json");
	xhr.send();
}

setTimeout(
	function () {
		current_load();
		setInterval(current_load, 60000);
	}, 1000
);

var table_node = document.getElementById("table");

function data_load() {
	var xhr = new XMLHttpRequest();
	xhr.onloadend = function () {
		if (xhr.status !== 200 || xhr.responseText == null)
			return alert("Failed to load weather data");
		try {
			var array = JSON.parse(xhr.responseText);
		}
		catch (e) {
			var array = null;
		}
		if (!Array.isArray(array))
			return alert("Invalid format of weather data file");
		table_node.textContent = null;
		array.forEach(
			function (record) {
				var tr = document.createElement("tr");
				for (var i = 0; i < 4; ++i) {
					var td = document.createElement("td");
					td.appendChild(document.createTextNode(record[i]));
					tr.appendChild(td);
				}
				table_node.appendChild(tr);
			}
		);
	};
	xhr.open("GET", "data.json");
	xhr.send();
}

var data_interval = null;

function data_schedule() {
	data_load();
	if (data_interval) clearInterval(data_interval);
	data_interval = setInterval(data_load, 60000);
}

setTimeout(data_schedule, 2000);

function position_load() {
	var xhr = new XMLHttpRequest();
	xhr.onloadend = function () {
		if (xhr.status !== 200 || xhr.responseText == null)
			return alert("Failed to load weather data");
		try {
			var array = JSON.parse(xhr.responseText);
		}
		catch (e) {
			var array = null;
		}
		if (!Array.isArray(array))
			return alert("Invalid format of weather data file");
	};
	xhr.open("GET", "position.json");
	xhr.send();
}

// setTimeout(
// 	function () {
// 		data_load();
// 		setInterval(position_load, 60000);
// 	}, 3000
// );

});
