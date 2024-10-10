(function(p){document.readyState!=="loading"?p():document.addEventListener("DOMContentLoaded",p)})(function(p){"use strict";

/* ************************************************************************** / ************************************** / **** */

function iso_date(date_string, seperator="T") {
	var date = new Date(date_string);
	return (
		date.getFullYear().toString().padStart(4, '0') + 
		"-" +
		(date.getMonth() + 1).toString().padStart(2, '0') + 
		"-" +
		date.getDate().toString().padStart(2, '0') + 
		seperator +
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
var $query_filter = document.getElementById("query-filter");
var $query_select = document.getElementById("query-select");
var $query_load = document.getElementById("query-load");
var $query_empty = document.getElementById("query-empty")
var $query_plot = document.getElementById("query-plot");
var $query_element = $query_plot.querySelector("select");
var $query_plotly = $query_plot.querySelector("div");
var $query_table = $query.querySelector("table");
var $query_tbody = $query.querySelector("tbody");

var selections = new Set;
var query_data = null;

function Selection() {
	this.$form = document.createElement("form");
		var $fieldset = document.createElement("fieldset");
			var $legend = document.createElement("legend");
				$legend.appendChild(document.createTextNode("Select device"));
			$fieldset.appendChild($legend);
			this.$load = document.createElement("button");
				this.$load.hidden = true;
				this.$load.setAttribute("type", "button");
				this.$load.appendChild(document.createTextNode("Reload campaigns"));
			$fieldset.appendChild(this.$load);
			var $label = document.createElement("label");
				$label.hidden = true;
				$label.appendChild(document.createTextNode("Campaign:"));
				this.$campaign = document.createElement("select");
					this.$campaign.setAttribute("name", "campaign");
				$label.appendChild(this.$campaign);
			$fieldset.appendChild($label);
			var $label = document.createElement("label");
				$label.hidden = true;
				$label.appendChild(document.createTextNode("Organisation:"));
				this.$organisation = document.createElement("select");
					this.$organisation.setAttribute("name", "organisation");
				$label.appendChild(this.$organisation);
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
			this.$download = document.createElement("a");
				this.$download.hidden = true;
				this.$download.setAttribute("href", "#");
				this.$download.appendChild(document.createTextNode("Download CSV"));
			$fieldset.appendChild(this.$download);
			var $span = document.createElement("span");
				$span.className = "grow";
			$fieldset.appendChild($span);
			this.$remove = document.createElement("button");
				this.$load.setAttribute("type", "button");
				this.$remove.appendChild(document.createTextNode("Remove"));
				if (!selections.size) this.$remove.disabled = true;
				this.$remove.addEventListener(
					"click",
					(event) => {
						event.preventDefault();
						this.remove();
					}
				);
			$fieldset.appendChild(this.$remove);
		this.$form.appendChild($fieldset);
	this.$parent.appendChild(this.$form);
	this.$form.addEventListener("submit", (event) => event.preventDefault());
	this.$load.addEventListener("click", this.load_campaigns.bind(this));
	this.$organisation.addEventListener("change", this.load_campaigns.bind(this));
	this.$campaign.addEventListener("change", this.load_devices.bind(this));
	this.load_campaigns();
	selections.add(this);
	return this;
}
Selection.prototype = {
	$parent: document.getElementById("selections"),
	combined_URL() {
		return (
			"combined/"
				+ encodeURIComponent(this.$campaign.value)
				+ "/"
				+ encodeURIComponent(this.$organisation.value)
				+ "/"
				+ encodeURIComponent(this.$device.value)
				+ ".csv"
		);
	},
	remove() {
		this.$parent.removeChild(this.$form);
		selections.delete(this);
		if (!selections.size) {
			$query_table.hidden = true;
			$query_tbody.textContent = null;
		}
		else if (selections.size === 1)
			selections.forEach(
				function (selection) {
					selection.$remove.disabled = true;
				}
			);
	},
	load_campaigns() {
		this.$device.parentElement.hidden = true;
		this.$campaign.parentElement.hidden = true;
		this.$organisation.parentElement.hidden = true;
		this.$loading.hidden = false;
		return (
			fetch("list/campaigns.txt")
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
					this.$download.hidden = true;
				}
			)
			.then(
				() => this.load_organisations().catch(() => {})
			)
			.catch(
				(error) => {
					alert("Failed to load campaign list: " + String(error));
					console.error(error);
				}
			)
		);
	},
	load_organisations() {
		this.$device.parentElement.hidden = true;
		this.$organisation.parentElement.hidden = true;
		this.$loading.hidden = false;
		return (
			fetch("list/" + encodeURIComponent(this.$campaign.value) + "/organisations.txt")
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
					this.$download.hidden = true;
				}
			)
			.then(
				() => this.load_devices().catch(() => {})
			)
			.catch(
				(error) => {
					alert("Failed to load organisation list: " + String(error));
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
					+ encodeURIComponent(this.$campaign.value)
					+ "/"
					+ encodeURIComponent(this.$organisation.value)
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
					this.$download.href = this.combined_URL() + data_filter_search();
					this.$download.hidden = false;
				}
			)
			.then(
				() => {
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

function selections_all_values() {
	return Array.from(
		selections.values(),
		function (selection) {
			return selection.values();
		}
	);
}

function selections_update_download() {
	var search = data_filter_search();
	selections.forEach(
		function (selection) {
			selection.$download.href = selection.combined_URL() + search;
		}
	);
}

new Selection;

$query_select.addEventListener(
	"submit",
	(event) => {
		event.preventDefault();
		if (selections.size === 1)
			selections.forEach(
				function (selection) {
					selection.$remove.disabled = false;
				}
			);
		new Selection;
	}
);

$query_filter.elements.namedItem("begin").value = iso_date(Date.now() - 24*60*60*1000);

function data_hide() {
	$query_empty.hidden = true;
	$query_plot.hidden = true;
	$query_table.hidden = true;
}

function data_plot() {
	if (query_data === null) {
		$query_plot.hidden = true;
		return;
	}
	$query_plot.hidden = false;
	Plotly.react(
		$query_plotly,
		query_data.map(
			function (records) {
				var column = Number.parseInt($query_element.value);
				var filtered =
					records.filter(
						function (record) {
							return record[column] != null && record[column] !== "";
						}
					);
				return {
					x: filtered.map(function (record) {return record[3];}),
					y: filtered.map(function (record) {return record[column];}),
					name: records[0][0] + "," + records[0][1] + "," + records[0][2]
				}
			}
		),
		{
			title: $query_element.selectedOptions.item(0).textContent
		}
	);
}

function data_list() {
	if (query_data === null) {
		$query_table.hidden = true;
		return;
	}
	$query_tbody.textContent = null;
	query_data.forEach(
		function (records) {
			records.forEach(
				function (record) {
					var $tr = document.createElement("tr");
					[0, 1, 2, 3, 6, 7, /* 8, */ 11, 12, 13].forEach(
						function (i) {
							var $td = document.createElement("td");
							$td.appendChild(document.createTextNode(record[i]));
							$tr.appendChild($td);
						}
					);
					$query_tbody.appendChild($tr);
				}
			);
		}
	);
	$query_table.hidden = false;
}

function data_show() {
	data_plot();
	data_list();
}

function data_filter_search() {
	var begin_time = $query_filter.elements.namedItem("begin").value;
	if (begin_time) begin_time = iso_date(begin_time);
	var end_time = $query_filter.elements.namedItem("end").value;
	if (end_time) end_time = iso_date(end_time);
	if (!begin_time && !end_time)
		return "";
	if (begin_time && end_time && begin_time > end_time)
		[begin_time, end_time] = [end_time, begin_time];
	var param = new URLSearchParams();
	if (begin_time) param.append("begin", begin_time);
	if (end_time) param.append("end", end_time);
	return "?" + param.toString();
}

function data_load() {
	data_hide();
	var search = data_filter_search();
	return Promise.all(
		selections.values().map(
			function (selection) {
				return (
					fetch(selection.combined_URL() + search)
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
							return Promise.resolve(
								lines.slice(1).map(
									function (line) {
										return line.split(",");
									}
								)
							);
						}
					)
				);
			}
		)
	)
	.then(
		(array) => {
			if (!array.some(function (a) {return Boolean(a.length);})) {
				query_data = null;
				$query_empty.hidden = false;
				return;
			}
			query_data = array;
			data_show();
			return Promise.resolve();
		}
	)
	.catch(
		(error) => {
			alert("Failed to load data: " + String(error));
			console.error(error);
			return null;
		}
	);
}

$query_filter.elements.namedItem("begin").addEventListener("change", selections_update_download);
$query_filter.elements.namedItem("end").addEventListener("change", selections_update_download);

$query_filter.addEventListener(
	"submit",
	(event) => {
		event.preventDefault();
		data_load();
	}
);

$query_load.addEventListener(
	"submit",
	(event) => {
		event.preventDefault();
		data_load();
	}
);

$query_element.addEventListener("change", data_plot);

/**************************************************************************** / ************************************** / ******/

});
