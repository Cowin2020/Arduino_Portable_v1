"use strict";

var map = L.map(
	"map",
	{
		center: L.latLng(22.35, 114.130),
		zoom: 12,
		zoomSnap: 0.25,
		zoomDelta: 0.25
	}
)

var tile_layer = new L.TileLayer(
	"https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png",
	{
		attribution: "Map data &#xA9; <a href='https://www.openstreetmap.org/about/'>OpenStreetMap</a>"
	}
)
map.addLayer(tile_layer);

var marker_layer = new L.LayerGroup();
map.addLayer(marker_layer);

var devices = new Array;
void function () {
	var tbody = document.querySelector("tbody");
	for (var i = 0; i < tbody.children.length; ++i) {
		var tr = tbody.children.item(i);
		var latitude = Number.parseFloat(tr.children.item(2).textContent);
		var longitude = Number.parseFloat(tr.children.item(3).textContent);
		if (latitude != null && longitude != null)
			devices.push(
				{
					identity: tr.children.item(0).textContent,
					latitude: latitude,
					longitude: longitude
				}
			);
	}
}();

devices.forEach(
	function (device) {
		var tooltip = L.tooltip(
			L.latLng(device.latitude, device.longitude),
			{content: device.identity}
		);
		tooltip.openOn(map);
	}
);
