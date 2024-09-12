import config

import csv
import json
import urllib.parse
import http
import http.server
import sqlite3

current_fields = ["time", "latitude", "longitude", "altitude"]
id_field = "identity"

current = dict()

try:
	with open("homepage.html", "rb") as file:
		homepage = file.read()
except:
	homepage = None

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

try:
	with open("favicon.ico", "rb") as file:
		favicon = file.read()
except:
	favicon = bytes(
		[
			0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
			0x00, 0x00, 0x00, 0x0D,
			0x49, 0x48, 0x44, 0x52,
			0x00, 0x00, 0x00, 0x01,
			0x00, 0x00, 0x00, 0x01,
			0x01, 0x00, 0x00, 0x00, 0x00,
			0x37, 0x6E, 0xF9, 0x24,
			0x00, 0x00, 0x00, 0x0A,
			0x49, 0x44, 0x41, 0x54,
			0x78, 0x01,
			0x63, 0x60, 0x00, 0x00,
			0x00, 0x02, 0x00, 0x01,
			0x73, 0x75, 0x01, 0x18])

database = sqlite3.connect(config.database)

class Handler(http.server.BaseHTTPRequestHandler):
	def content(self):
		try:
			length = int(self.headers.get("CONTENT-LENGTH"))
		except:
			length = -1
		return self.rfile.read(length)
	def get_data_DB(self, table_name, fields, url):
		query = urllib.parse.parse_qs(url.query)
		sql = "SELECT device, time, " + ", ".join(fields) +  " FROM " + table_name
		bindings = []
		device = query.get("device")
		if device:
			sql = sql + " WHERE device = ?"
			bindings.append(device)
		begin = query.get("begin")
		sql = sql + " ORDER BY time ASC"
		cursor = database.cursor()
		cursor.execute(sql, bindings)
		return cursor
	def get_data_JSON(self, table_name, fields, url):
		cursor = self.get_data_DB(table_name, fields, url)
		self.send_response_only(http.HTTPStatus.OK, "OK")
		self.send_header("CONTENT-TYPE", "application/json")
		self.end_headers()
		self.wfile.write(bytes(json.dumps(list(cursor)), "UTF-8"))
	def get_data_CSV(self, table_name, fields, url):
		cursor = self.get_data_DB(table_name, fields, url)
		self.send_response_only(http.HTTPStatus.OK, "OK")
		self.send_header("CONTENT-TYPE", "text/csv; charset=UTF-8")
		self.end_headers()
		self.wfile.write(b"device,time,")
		self.wfile.write(bytes(",".join(fields) + "\n", "UTF-8"))
		for row in cursor:
			self.wfile.write(bytes(",".join(row) + "\n", "UTF-8"))
	def post_data(self, table_name, fields):
		body = self.content()
		lines = body.decode("UTF-8").split("\r\n")
		if len(lines) < 2:
			return self.send_error(400, "malformat", "invalid number of rows")
		device = lines[0]
		parsed = list(csv.reader(lines[1:]))
		rows = parsed[1:]
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
		self.send_response_only(http.HTTPStatus.OK, "OK")
		self.send_header("CONTENT-TYPE", "text/plain")
		self.send_header("ACCESS-CONTROL-ALLOW-ORIGIN", "*")
		self.end_headers()
	def do_GET(self):
		self.log_request()
		if homepage and self.path == "/":
			self.send_response_only(http.HTTPStatus.OK, "OK")
			self.send_header("CONTENT-TYPE", "application/xhtml+xml")
			self.end_headers()
			self.wfile.write(homepage)
		elif style and self.path == "/style.css":
			self.send_response_only(http.HTTPStatus.OK, "OK")
			self.send_header("CONTENT-TYPE", "text/css")
			self.end_headers()
			self.wfile.write(style)
		elif script and self.path == "/script.js":
			self.send_response_only(http.HTTPStatus.OK, "OK")
			self.send_header("CONTENT-TYPE", "text/javascript")
			self.end_headers()
			self.wfile.write(script)
		elif favicon and self.path == "/favicon.ico":
			self.send_response_only(http.HTTPStatus.OK, "OK")
			self.send_header("CONTENT-TYPE", "image/png")
			self.end_headers()
			self.wfile.write(favicon)
		elif self.path == "/devices.txt":
			self.send_response_only(http.HTTPStatus.OK, "OK")
			self.send_header("CONTENT-TYPE", "text/plain")
			self.end_headers()
			cursor = database.cursor()
			cursor.execute("SELECT DISTINCT device FROM data ORDER BY device ASC")
			for device in cursor:
				self.wfile.write(bytes(device[0], "UTF-8"))
				self.wfile.write(bytes("\n", "UTF-8"))
		elif self.path == "/current.json":
			self.send_response_only(http.HTTPStatus.OK, "OK")
			self.send_header("CONTENT-TYPE", "application/json")
			self.end_headers()
			self.wfile.write(bytes(json.dumps(current), "UTF-8"))
		else:
			url = urllib.parse.urlparse(self.path)
			if url.path == "/data.json":
				self.get_data_JSON("data", ["temperature", "humidity"], url)
			elif url.path == "/position.json":
				self.get_data_JSON("position", ["latitude", "longitude", "altitude"], url)
			elif url.path == "/data.csv":
				self.get_data_CSV("data", ["temperature", "humidity"], url)
			elif url.path == "/position.csv":
				self.get_data_CSV("position", ["latitude", "longitude", "altitude"], url)
			else:
				self.send_error(404)
	def do_POST(self):
		self.log_request()
		if self.path == "/report":
			body = self.content()
			queries = {k: v[0] for k, v in urllib.parse.parse_qs(body.decode("UTF-8")).items()}
			identity = queries.get(id_field)
			if identity is None:
				self.send_error(400, "INCORRECT CONTENT", "identity is missed")
				return
			current[identity] = [queries.get(field) for field in current_fields]
			self.send_response_only(http.HTTPStatus.NO_CONTENT)
			self.end_headers()
		elif self.path == "/upload/data":
			self.post_data("data", ["temperature", "humidity"])
		elif self.path == "/upload/position":
			self.post_data("position", ["latitude", "longitude", "altitude"])
		else:
			self.send_error(404)

print("HTTPd listen on \"{}\" port {}".format(config.HOST, config.PORT))
httpd = http.server.HTTPServer((config.HOST, config.PORT), Handler)

try:
	httpd.serve_forever()
except KeyboardInterrupt:
	pass
