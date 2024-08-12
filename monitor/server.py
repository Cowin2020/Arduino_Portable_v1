import config

import csv
import json
import urllib.parse
import http
import http.server
import sqlite3

data_fields = ["time", "latitude", "longitude", "altitude"]
id_fields = "identity"

current = dict()

try:
	with open("script.js", "rb") as file:
		script = file.read()
except:
	script = None

try:
	with open("style.css", "rb") as file:
		style = file.read()
except:
	style = None

class Handler(http.server.BaseHTTPRequestHandler):
	def content(self):
		try:
			length = int(self.headers.get("CONTENT-LENGTH"))
		except:
			length = -1
		return self.rfile.read(length)
	def get_data(self, table_name, fields, url):
		query = urllib.parse.parse_qs(url.query)
		device = query.get("device")
		if not device:
			return self.send_error(400, "missing parameter", "missing device name")
		database = sqlite3.connect(config.database)
		try:
			cursor = database.cursor()
			cursor.execute(
				"SELECT device, time, " + ", ".join(fields) +  " FROM " + table_name
					+ " WHERE device = ? ORDER BY time ASC",
				[device[0]])
			table = list(cursor)
		finally:
			database.close()
		self.send_response_only(http.HTTPStatus.OK, "OK")
		self.send_header("CONTENT-TYPE", "application/json")
		self.end_headers()
		self.wfile.write(bytes(json.dumps(table), "UTF-8"))
	def post_data(self, table_name, fields):
		body = self.content()
		lines = body.decode("UTF-8").split("\r\n")
		if len(lines) < 2:
			return self.send_error(400, "malformat", "invalid number of rows")
		device = lines[0]
		parsed = list(csv.reader(lines[1:]))
		rows = parsed[1:]
		database = sqlite3.connect(config.database)
		try:
			cursor = database.cursor()
			for row in rows:
				if len(row) != len(fields) + 1:
					continue
				cursor.execute(
					"INSERT INTO " + table_name + " (device, time, " + ", ".join(fields) + ") "
						"VALUES (?, ?, " + ", ".join(map(lambda x: "?", fields)) + ") "
						"ON CONFLICT DO NOTHING",
					[device] + row)
			database.commit()
		finally:
			database.close()
		self.send_response_only(http.HTTPStatus.OK, "OK")
		self.send_header("CONTENT-TYPE", "text/plain")
		self.end_headers()
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
			for name in current:
				self.wfile.write(b"<tr><td>")
				self.wfile.write(bytes(name, "UTF-8"))
				self.wfile.write(b"</td>")
				for field in current[name]:
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
		elif self.path == "/style.css" and style:
			self.send_response_only(http.HTTPStatus.OK, "OK")
			self.send_header("CONTENT-TYPE", "text/css")
			self.end_headers()
			self.wfile.write(style)
		elif self.path == "/script.js" and script:
			self.send_response_only(http.HTTPStatus.OK, "OK")
			self.send_header("CONTENT-TYPE", "text/javascript")
			self.end_headers()
			self.wfile.write(script)
		else:
			url = urllib.parse.urlparse(self.path)
			if url.path == "/current":
				self.send_response_only(http.HTTPStatus.OK, "OK")
				self.send_header("CONTENT-TYPE", "application/json")
				self.end_headers()
				self.wfile.write(bytes(json.dumps(current), "UTF-8"))
			elif url.path == "/data":
				self.get_data("data", ["temperature", "humidity"], url)
			elif url.path == "/position":
				self.get_data("position", ["latitude", "longitude", "altitude"], url)
			else:
				self.send_error(404)
	def do_POST(self):
		self.log_request()
		if self.path == "/report":
			body = self.content()
			queries = {k: v[0] for k, v in urllib.parse.parse_qs(body.decode("UTF-8")).items()}
			identity = queries.get(id_fields)
			if identity is None:
				self.send_error(400, "INCORRECT CONTENT", "identity is missed")
				return
			current[identity] = [queries.get(field) for field in data_fields]
			self.send_response_only(http.HTTPStatus.NO_CONTENT)
			self.end_headers()
		elif self.path == "/upload/data":
			self.post_data("data", ["temperature", "humidity"])
		elif self.path == "/upload/position":
			self.post_data("position", ["latitude", "longitude", "altitude"])
		else:
			self.send_error(404)

httpd = http.server.ThreadingHTTPServer(("", config.PORT), Handler)
httpd.serve_forever()
