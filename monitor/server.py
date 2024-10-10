import config

import re
import csv
import json
import urllib.parse
import http
import http.server
import ssl
import sqlite3

current_fields = ["time", "latitude", "longitude", "altitude"]
data_fields = ["device_time", "clock_synchronized", "temperature", "humidity", "pressure"]
position_fields = ["browser_time", "position_time", "latitude", "longitude", "altitude"]

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

def to_CSV_value(x):
	if x is None:
		return ""
	else:
		return str(x)

database = sqlite3.connect(config.database)

def select_data(table_name, fields, match, query):
	campaign = match[1]
	organisation = match[2]
	device = match[3]
	select = " FROM " + table_name
	select = select + " WHERE campaign = ? AND organisation = ? AND device = ?"
	bindings = [campaign, organisation, device]
	parameter = query.get("begin", [None])[0]
	if parameter:
		select = select + " AND time >= ?"
		bindings.append(parameter)
	parameter = query.get("end", [None])[0]
	if parameter:
		select = select + " AND time <= ?"
		bindings.append(parameter)
	select = select + " ORDER BY time ASC"
	cursor = database.cursor()
	cursor.execute("SELECT COUNT(*) " + select, bindings)
	count = cursor.fetchone()[0]
	cursor = database.cursor()
	cursor.execute("SELECT " + "campaign, organisation, device, time, " + ", ".join(fields) + select, bindings)
	return (cursor, count)

class Handler(http.server.BaseHTTPRequestHandler):
	def content(self):
		try:
			length = int(self.headers.get("CONTENT-LENGTH"))
		except:
			length = -1
		return self.rfile.read(length)

	def get_data_JSON(self, table_name, fields, match, query):
		(cursor, count) = select_data(table_name, fields, match, query)
		self.send_response_only(http.HTTPStatus.OK, "OK")
		self.send_header("CONTENT-TYPE", "application/json")
		self.send_header("X-TOTAL-COUNT", str(count))
		self.end_headers()
		self.wfile.write(bytes(json.dumps(list(cursor)), "UTF-8"))

	def get_data_CSV(self, table_name, fields, match, query):
		(cursor, count) = select_data(table_name, fields, match, query)
		self.send_response_only(http.HTTPStatus.OK, "OK")
		self.send_header("CONTENT-TYPE", "text/csv; charset=UTF-8")
		self.send_header("X-TOTAL-COUNT", str(count))
		self.end_headers()
		self.wfile.write(b"campaign,organisation,device,time,")
		self.wfile.write(bytes(",".join(fields) + "\n", "UTF-8"))
		for row in cursor:
			self.wfile.write(bytes(",".join(map(to_CSV_value, row)), "UTF-8"))
			self.wfile.write(b"\n")

	def get_combined_CSV(self, match, query):
		sql = (
			"SELECT data.campaign, data.organisation, data.device, data.time, " +
			", ".join(data_fields + position_fields) +
			" FROM "
				" data"
					" LEFT OUTER JOIN position ON "
						" position.campaign = data.campaign AND"
						" position.organisation = data.organisation AND"
						" position.device = data.device AND"
						" position.time = data.time"
				" WHERE data.campaign = ? AND data.organisation = ? AND data.device = ?"
		)
		bindings = [match.group(1), match.group(2), match.group(3)]
		parameter = query.get("begin", [None])[0]
		if parameter:
			sql = sql + " AND data.time >= ?"
			bindings.append(parameter)
		parameter = query.get("end", [None])[0]
		if parameter:
			sql = sql + " AND data.time <= ?"
			bindings.append(parameter)
		sql = sql + " ORDER BY data.time ASC"
		cursor = database.cursor()
		cursor.execute(sql, bindings)
		self.send_response_only(http.HTTPStatus.OK, "OK")
		self.send_header("CONTENT-TYPE", "text/csv; charset=UTF-8")
		self.end_headers()
		self.wfile.write(b"campaign,organisation,device,time,")
		self.wfile.write(bytes(",".join(data_fields), "UTF-8"))
		self.wfile.write(b",")
		self.wfile.write(bytes(",".join(position_fields), "UTF-8"))
		self.wfile.write(b"\n")
		for row in cursor:
			self.wfile.write(bytes(",".join(map(to_CSV_value, row)), "UTF-8"))
			self.wfile.write(b"\n")

	def post_data(self, table_name, fields):
		body = self.content()
		lines = body.decode("UTF-8").split("\r\n")
		if len(lines) < 5:
			return self.send_error(400, "too few rows: " + len(lines))
		campaign = lines[0]
		organisation = lines[1]
		device = lines[2]
		password = lines[3]
		parsed = list(csv.reader(lines[4:]))
		rows = parsed[1:]
		cursor = database.cursor()
		cursor.execute("SELECT password FROM password WHERE campaign = ?", (campaign,))
		if all(map(lambda row: row[0] != password, cursor)):
			self.send_error(403)
		cursor = database.cursor()
		for row in rows:
			if len(row) != len(fields) + 1:
				continue
			cursor.execute(
				"INSERT INTO " + table_name + " (campaign, organisation, device, time, " + ", ".join(fields) + ") "
					"VALUES (?, ?, ?, ?, " + ", ".join(map(lambda x: "?", fields)) + ") "
					"ON CONFLICT DO NOTHING",
				[campaign, organisation, device] + row)
		database.commit()
		self.send_response_only(http.HTTPStatus.OK, "OK")
		self.send_header("CONTENT-TYPE", "text/plain")
		self.send_header("ACCESS-CONTROL-ALLOW-ORIGIN", "*")
		self.end_headers()

	pattern_list_organisations = re.compile(r"/list/([^/]+)/organisations.txt")
	pattern_list_devices = re.compile(r"/list/([^/]+)/([^/]+)/devices.txt")
	pattern_data_JSON = re.compile(r"/data/([^/]+)/([^/]+)/(.+)\.json")
	pattern_position_JSON = re.compile(r"/position/([^/]+)/([^/]+)/(.+)\.json")
	pattern_data_CSV = re.compile(r"/data/([^/]+)/([^/]+)/(.+)\.csv")
	pattern_position_CSV = re.compile(r"/position/([^/]+)/([^/]+)/(.+)\.csv")
	pattern_combined_CSV = re.compile(r"/combined/([^/]+)/([^/]+)/(.+)\.csv(?:[?#].*)?")

	def do_GET(self):
		self.log_request()
		url = urllib.parse.urlparse(self.path)
		if homepage and url.path == "/":
			self.send_response_only(http.HTTPStatus.OK, "OK")
			#	self.send_header("CONTENT-TYPE", "application/xhtml+xml")
			self.send_header("CONTENT-TYPE", "text/html")
			self.end_headers()
			self.wfile.write(homepage)
			return
		if style and url.path == "/style.css":
			self.send_response_only(http.HTTPStatus.OK, "OK")
			self.send_header("CONTENT-TYPE", "text/css")
			self.end_headers()
			self.wfile.write(style)
			return
		if script and url.path == "/script.js":
			self.send_response_only(http.HTTPStatus.OK, "OK")
			self.send_header("CONTENT-TYPE", "text/javascript")
			self.end_headers()
			self.wfile.write(script)
			return
		if favicon and url.path == "/favicon.ico":
			self.send_response_only(http.HTTPStatus.OK, "OK")
			self.send_header("CONTENT-TYPE", "image/png")
			self.end_headers()
			self.wfile.write(favicon)
			return
		if url.path == "/current.json":
			self.send_response_only(http.HTTPStatus.OK, "OK")
			self.send_header("CONTENT-TYPE", "application/json")
			self.end_headers()
			self.wfile.write(bytes(json.dumps(current), "UTF-8"))
			return
		if url.path == "/list/campaigns.txt":
			self.send_response_only(http.HTTPStatus.OK, "OK")
			self.send_header("CONTENT-TYPE", "text/plain")
			self.end_headers()
			cursor = database.cursor()
			cursor.execute("SELECT DISTINCT campaign FROM data ORDER BY campaign ASC")
			for record in cursor:
				self.wfile.write(bytes(record[0], "UTF-8"))
				self.wfile.write(b"\n")
			return
		match = self.pattern_list_organisations.fullmatch(url.path)
		if match:
			self.send_response_only(http.HTTPStatus.OK, "OK")
			self.send_header("CONTENT-TYPE", "text/plain")
			self.end_headers()
			cursor = database.cursor()
			cursor.execute(
				"SELECT DISTINCT organisation "
					"FROM data "
					"WHERE campaign = ? "
					"ORDER BY organisation ASC",
				[match.group(1)])
			for record in cursor:
				self.wfile.write(bytes(record[0], "UTF-8"))
				self.wfile.write(b"\n")
			return
		match = self.pattern_list_devices.fullmatch(url.path)
		if match:
			self.send_response_only(http.HTTPStatus.OK, "OK")
			self.send_header("CONTENT-TYPE", "text/plain")
			self.end_headers()
			cursor = database.cursor()
			cursor.execute(
				"SELECT DISTINCT device "
					"FROM data "
					"WHERE campaign = ? AND organisation = ?"
					"ORDER BY device ASC",
				[match.group(1), match.group(2)])
			for record in cursor:
				self.wfile.write(bytes(record[0], "UTF-8"))
				self.wfile.write(b"\n")
			return
		query = urllib.parse.parse_qs(url.query)
		match = self.pattern_data_JSON.fullmatch(url.path)
		if match:
			self.get_data_JSON("data", data_fields, match, query)
			return
		match = self.pattern_position_JSON.fullmatch(url.path)
		if match:
			self.get_data_JSON("position", position_fields, match, query)
			return
		match = self.pattern_data_CSV.fullmatch(url.path)
		if match:
			self.get_data_CSV("data", data_fields, match, query)
			return
		match = self.pattern_position_CSV.fullmatch(url.path)
		if match:
			self.get_data_CSV("position", position_fields, match, query)
			return
		match = self.pattern_combined_CSV.fullmatch(url.path)
		if match:
			self.get_combined_CSV(match, query)
			return
		self.send_error(404)

	def do_POST(self):
		self.log_request()
		if self.path == "/report":
			body = self.content()
			queries = {k: v[0] for k, v in urllib.parse.parse_qs(body.decode("UTF-8")).items()}
			campaign = queries.get("campaign")
			if campaign is None:
				self.send_error(400, "campaign is missed")
				return
			organisation = queries.get("organisation")
			if organisation is None:
				self.send_error(400, "organisation is missed")
				return
			device = queries.get("device")
			if device is None:
				self.send_error(400, "device is missed")
				return
			current[device] = [queries.get(field) for field in current_fields]
			self.send_response_only(http.HTTPStatus.NO_CONTENT)
			self.send_header("ACCESS-CONTROL-ALLOW-ORIGIN", "*")
			self.end_headers()
		elif self.path == "/upload/data":
			self.post_data("data", data_fields)
		elif self.path == "/upload/position":
			self.post_data("position", position_fields)
		else:
			self.send_error(404)

print("Web server listen on {}:{}".format(config.HOST, config.PORT))
httpd = http.server.HTTPServer((config.HOST, config.PORT), Handler)

if config.SSL_key and config.SSL_cert:
	httpd.socket = ssl.wrap_socket(
		httpd.socket,
		server_side=True,
		certfile=config.SSL_cert,
		keyfile=config.SSL_key,
		ssl_version=ssl.PROTOCOL_TLS)

try:
	httpd.serve_forever()
except KeyboardInterrupt:
	pass
