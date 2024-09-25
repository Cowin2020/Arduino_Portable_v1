(function(p){document.readyState!=="loading"?p():document.addEventListener("DOMContentLoaded",p)})(function(p){"use strict";

/* ************************************************************************** / ************************************** / **** */

function renormal_iso_date(date_string) {
	var date = new Date(date_string);
	return (
		date.getFullYear().toString().padStart(4, '0') + 
		"-" +
		(date.getMonth() + 1).toString().padStart(2, '0') + 
		"-" +
		date.getDate().toString().padStart(2, '0') + 
		"T" +
		date.getHours().toString().padStart(2, '0') + 
		":" +
		date.getMinutes().toString().padStart(2, '0') + 
		":" +
		date.getSeconds().toString().padStart(2, '0')
	);
}

/* ************************************************************************** / ************************************** / **** */
/*** Current position */

function Current(element_id) {
	this.map = L.map(
		element_id,
		{
			center: L.latLng(22.35, 114.130),
			zoom: 12,
			zoomSnap: 0.25,
			zoomDelta: 0.25
		}
	);
	this.map.addLayer(
		new L.TileLayer(
			"https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png",
			{
				attribution: "Map data &#xA9; <a href='https://www.openstreetmap.org/about/'>OpenStreetMap</a>"
			}
		)
	);
	this.map.addLayer(new L.LayerGroup());
	this.tooltips = new Array();
	this.interval = null;
	return this;
}
Current.prototype = {
	clear() {
		this.tooltips.forEach(
			function (tooltip) {
				this.map.removeLayer(tooltip);
			},
			this
		);
	},
	plot(devices) {
		devices.forEach(
			function (device) {
				var tooltip = L.tooltip(
					L.latLng(device.latitude, device.longitude),
					{
						permanent: true,
						content: device.identity
					}
				);
				this.tooltips.push(tooltip);
				tooltip.openOn(this.map);
			},
			this
		);
	},
	load() {
		return (
			fetch("current.json")
			.then(
				(response) => {
					if (!response.ok)
						return Promise.reject(String(response.status) + " from server");
					return response.text();
				}
			)
			.then(
				(text) => {
					try {
						return Promise.resolve(JSON.parse(text));
					}
					catch (e) {
						return Promise.resolve(null);
					}
				}
			)
			.then(
				(object) => {
					if (typeof object !== "object")
						return Promise.reject("invalid file format");
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
					this.clear();
					this.plot(devices);
				}
			)
			.catch(
				(error) => {
					alert("Failed to load current position: " + String(error));
					console.error(error);
				}
			)
		);
	},
	run(interval, delay) {
		setTimeout(
			function () {
				this.load();
				if (this.interval) clearInterval(this.interval);
				setInterval(this.load.bind(this), interval);
			}.bind(this),
			delay
		);
	}
};

var current = new Current("map");

current.run(60000, 1000);

/* ************************************************************************** / ************************************** / **** */
/*** Uploaded data ***/

var $query = document.getElementById("query");
var $query_select = document.getElementById("query-select");
var $query_filter = document.getElementById("query-filter");
var $table = $query.querySelector("table");
var $tbody = $query.querySelector("tbody");

var selections = new Set;

function Selection() {
	this.$form = document.createElement("form");
		var $fieldset = document.createElement("fieldset");
			var $legend = document.createElement("legend");
				$legend.appendChild(document.createTextNode("Select device"));
			$fieldset.appendChild($legend);
			this.$load = document.createElement("button");
				this.$load.hidden = true;
				this.$load.setAttribute("type", "button");
				this.$load.appendChild(document.createTextNode("Reload organisations"));
			$fieldset.appendChild(this.$load);
			var $label = document.createElement("label");
				$label.hidden = true;
				$label.appendChild(document.createTextNode("Organisation:"));
				this.$organisation = document.createElement("select");
					this.$organisation.setAttribute("name", "organisation");
				$label.appendChild(this.$organisation);
			$fieldset.appendChild($label);
			var $label = document.createElement("label");
				$label.hidden = true;
				$label.appendChild(document.createTextNode("Campaign:"));
				this.$campaign = document.createElement("select");
					this.$campaign.setAttribute("name", "campaign");
				$label.appendChild(this.$campaign);
			$fieldset.appendChild($label);
			var $label = document.createElement("label");
				$label.hidden = true;
				$label.appendChild(document.createTextNode("Device:"));
				this.$device = document.createElement("select");
					this.$device.setAttribute("name", "device");
				$label.appendChild(this.$device);
			$fieldset.appendChild($label);
			this.$loading = document.createElement("span");
				this.$loading.appendChild(document.createTextNode("Loading..."));
			$fieldset.appendChild(this.$loading);
			var $span = document.createElement("span");
				$span.style.setProperty("flex-grow", "1");
			$fieldset.appendChild($span);
			this.$remove = document.createElement("button");
				this.$load.setAttribute("type", "submit");
				this.$remove.appendChild(document.createTextNode("Remove"));
			$fieldset.appendChild(this.$remove);
		this.$form.appendChild($fieldset);
	this.$parent.appendChild(this.$form);
	this.$form.addEventListener(
		"submit",
		(event) => {
			event.preventDefault();
			this.remove();
		}
	);
	this.$load.addEventListener("click", this.load_organisations.bind(this));
	this.$organisation.addEventListener("change", this.load_campaigns.bind(this));
	this.$campaign.addEventListener("change", this.load_devices.bind(this));
	this.load_organisations();
	selections.add(this);
	return this;
}
Selection.prototype = {
	$parent: document.getElementById("selections"),
	remove() {
		this.$parent.removeChild(this.$form);
		selections.delete(this);
		if (!selections.size) {
			$query_filter.hidden = true;
			$table.hidden = true;
			$tbody.textContent = null;
		}
	},
	values() {
		return {
			organisation: this.$organisation.value,
			campaign: this.$campaign.value,
			device: this.$device.value
		};
	},
	load_organisations() {
		this.$device.parentElement.hidden = true;
		this.$campaign.parentElement.hidden = true;
		this.$organisation.parentElement.hidden = true;
		this.$loading.hidden = false;
		return (
			fetch("list/organisations.txt")
			.then(
				(response) => {
					if (!response.ok)
						return Promise.reject(String(response.status) + " from server");
					return response.text();
				}
			)
			.then(
				(text) => {
					this.$loading.hidden = true;
					return Promise.resolve(text.split("\n").filter(Boolean, this));
				}
			)
			.then(
				(organisations) => {
					this.$organisation.textContent = null;
					organisations.forEach(
						function (organisation) {
							var $option = document.createElement("option");
							$option.setAttribute("value", organisation);
							$option.appendChild(document.createTextNode(organisation));
							this.$organisation.appendChild($option);
						},
						this
					);
					this.$organisation.parentElement.hidden = false;
				}
			)
			.then(
				() => this.load_campaigns().catch(() => {})
			)
			.catch(
				(error) => {
					alert("Failed to load organisation list: " + String(error));
					console.error(error);
				}
			)
		);
	},
	load_campaigns() {
		this.$device.parentElement.hidden = true;
		this.$campaign.parentElement.hidden = true;
		this.$loading.hidden = false;
		return (
			fetch("list/" + encodeURIComponent(this.$organisation.value) + "/campaigns.txt")
			.then(
				(response) => {
					if (!response.ok)
						return Promise.reject(String(response.status) + " from server");
					return response.text();
				}
			)
			.then(
				(text) => {
					this.$loading.hidden = true;
					return Promise.resolve(text.split("\n").filter(Boolean, this));
				}
			)
			.then(
				(campaigns) => {
					this.$campaign.textContent = null;
					campaigns.forEach(
						function (campaign) {
							var $option = document.createElement("option");
							$option.setAttribute("value", campaign);
							$option.appendChild(document.createTextNode(campaign));
							this.$campaign.appendChild($option);
						},
						this
					);
					this.$campaign.parentElement.hidden = false;
				}
			)
			.then(
				() => this.load_devices().catch(() => {})
			)
			.catch(
				(error) => {
					alert("Failed to load campaign list: " + String(error));
					console.error(error);
				}
			)
		);
	},
	load_devices() {
		this.$device.parentElement.hidden = true;
		this.$loading.hidden = false;
		return (
			fetch(
				"list/"
					+ encodeURIComponent(this.$organisation.value)
					+ "/"
					+ encodeURIComponent(this.$campaign.value)
					+ "/devices.txt"
			)
			.then(
				(response) => {
					if (!response.ok)
						return Promise.reject(String(response.status) + " from server");
					return response.text();
				}
			)
			.then(
				(text) => {
					this.$loading.hidden = true;
					return Promise.resolve(text.split("\n").filter(Boolean, this));
				}
			)
			.then(
				(devices) => {
					this.$device.textContent = null;
					devices.forEach(
						function (device) {
							var $option = document.createElement("option");
							$option.setAttribute("value", device);
							$option.appendChild(document.createTextNode(device));
							this.$device.appendChild($option);
						},
						this
					);
					this.$device.parentElement.hidden = false;
					$query_filter.hidden = false;
				}
			)
			.catch(
				(error) => {
					alert("Failed to load device list: " + String(error));
					console.error(error);
				}
			)
		);
	}
};

new Selection;
$query_select.addEventListener(
	"submit",
	(event) => {
		event.preventDefault();
		new Selection;
	}
);

function selections_all_values() {
	return Array.from(
		selections.values(),
		function (selection) {
			return selection.values();
		}
	);
}

function data_load() {
	$table.hidden = true;
	var search = (function () {
		var begin_time = $query_filter["begin"].value;
		if (begin_time) begin_time = renormal_iso_date(begin_time);
		var end_time = $query_filter["end"].value;
		if (end_time) end_time = renormal_iso_date(end_time);
		if (!begin_time && !end_time)
			return "";
		if (begin_time && end_time && begin_time > end_time)
			[begin_time, end_time] = [end_time, begin_time];
		var param = new URLSearchParams();
		if (begin_time) param.append("begin", begin_time);
		if (end_time) param.append("end", end_time);
		return "?" + param.toString();
	})();
	return Promise.all(
		selections.values().map(
			function (selection) {
				return Promise.resolve(
					selection.values()
				)
				.then(
					(values) =>
						fetch(
							"combined/"
								+ encodeURIComponent(values.organisation)
								+ "/"
								+ encodeURIComponent(values.campaign)
								+ "/"
								+ encodeURIComponent(values.device)
								+ ".csv"
								+ search
						)
				)
				.then(
					(response) => {
						if (!response.ok)
							return Promise.reject(String(response.status) + " from server");
						return response.text();
					}
				)
				.then(
					(text) => {
						var lines = text.split("\n").filter(Boolean);
						if (lines.length < 1)
							return Promise.reject("Invalid file format");
						return lines.slice(1).map(
							function (line) {
								return line.split(",");
							}
						);
					}
				)
			}
		)
	)
	.catch(
		(error) => {
			alert("Failed to load data: " + String(error));
			console.error(error);
		}
	);
}

function table_load() {
	return (
		data_load()
	)
	.then(
		(array) => {
			if (array.some(function (a) {return Boolean(a.length);})) {
				$tbody.textContent = null;
				array.forEach(
					function (records) {
						records.forEach(
							function (record) {
								var $tr = document.createElement("tr");
								[0, 1, 2, 3, 6, 7, 10, 11, 12].forEach(
									(i) => {
										var $td = document.createElement("td");
										$td.appendChild(document.createTextNode(record[i]));
										$tr.appendChild($td);
									}
								);
								$tbody.appendChild($tr);
							}
						);
					}
				);
				$table.hidden = false;
			}
		}
	)
}

$query_filter.addEventListener(
	"submit",
	(event) => {
		event.preventDefault();
		table_load();
	}
);

/*
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

setTimeout(
	function () {
		data_load();
		if (data_interval) clearInterval(data_interval);
		data_interval = setInterval(data_load, 60000);
	},
	2000
);

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
*/

/**************************************************************************** / ************************************** / ******/

});
