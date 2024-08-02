import os
import urllib.parse
import http
import http.server

try:
	PORT = int(os.environ["PORT"])
except:
	PORT = 8880

data_fields = ["time", "latitude", "longitude", "altitude"]
id_fields = "identity"

table = dict()

class Handler(http.server.BaseHTTPRequestHandler):
	def content(self):
		try:
			length = int(self.headers.get("CONTENT-LENGTH"))
		except:
			length = -1
		return self.rfile.read(length)
	def do_GET(self):
		self.log_request()
		if self.path == "/":
			self.send_response_only(http.HTTPStatus.OK, "OK")
			self.send_header("CONTENT-TYPE", "application/xhtml+xml")
			# self.send_header("CONTENT-TYPE", "text/html")
			self.end_headers()
			self.wfile.write(
				b"<html xmlns='http://www.w3.org/1999/xhtml'>"
				b"<head><meta encoding='UTF-8'/><title>Monitor</title>"
				b"<link rel='stylesheet' href='style.css' />"
				b"<link rel='stylesheet' href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css'"
				b" integrity='sha256-p4NxAoJBhIIN+hmNHrzRCf9tD/miZyoHS5obTRR9BMY=' crossorigin='' />"
				b"</head><body><table><thead><th>Identity</th>",
			)
			for field in data_fields:
				self.wfile.write(b"<th>")
				self.wfile.write(bytes(field, "UTF-8"))
				self.wfile.write(b"</th>")
			self.wfile.write(b"</thead><tbody>")
			for name in table:
				self.wfile.write(b"<tr><td>")
				self.wfile.write(bytes(name, "UTF-8"))
				self.wfile.write(b"</td>")
				for field in table[name]:
					self.wfile.write(b"<td>")
					if field is not None:
						self.wfile.write(bytes(field, "UTF-8"))
					self.wfile.write(b"</td>")
				self.wfile.write(b"</tr>")
			self.wfile.write(
				b"</tbody></table>"
				b"<hr />"
				b"<div id='map'></div>"
				b"<script type='text/javascript' src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js'"
				b" integrity='sha256-20nQCchB9co0qIjJZRGuk2/Z9VM+kNiyxNV1lvTlZBo=' crossorigin=''></script>"
				b"<script type='text/javascript' src='script.js'></script>"
				b"</body></html>"
			)
		elif self.path == "/style.css":
			self.send_response_only(http.HTTPStatus.OK, "OK")
			self.send_header("CONTENT-TYPE", "text/css")
			self.end_headers()
			self.wfile.write(
				b"table {"
					b"width:100%;"
					b"border-collapse:collapse}"
				b"td {"
					b"border:solid thin;"
					b"text-align:center}"
				b"#map {"
					b"height:90vh}"
			)
		elif self.path == "/script.js":
			self.send_response_only(http.HTTPStatus.OK, "OK")
			self.send_header("CONTENT-TYPE", "text/javascript")
			self.end_headers()
			self.wfile.write(
				b'''"use strict";

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
'''
			)
		else:
			self.send_error(404)
	def do_POST(self):
		self.log_request()
		if self.path == "/":
			body = self.content()
			queries = {k: v[0] for k, v in urllib.parse.parse_qs(body.decode("UTF-8")).items()}
			identity = queries.get(id_fields)
			if identity is None:
				self.send_error(400, "INCORRECT CONTENT", "Identity is missed")
				return
			table[identity] = [queries.get(field) for field in data_fields]
			self.send_response_only(http.HTTPStatus.NO_CONTENT)
			self.end_headers()
		else:
			self.send_error(404)

httpd = http.server.ThreadingHTTPServer(("", PORT), Handler)
httpd.serve_forever()