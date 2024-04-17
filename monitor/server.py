import sys
import os
import urllib.parse
import http
import http.server

try:
	PORT = int(os.environ["PORT"])
except:
	PORT = 8880

data_fields = ["time", "temperature", "pressure", "humidity"]
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
			self.end_headers()
			self.wfile.write(
				b"<html xmlns='http://www.w3.org/1999/xhtml'>"
				b"<head><meta encoding='UTF-8'/><title>Monitor</title>"
				b"<link rel='stylesheet' href='style.css' /></head>"
				b"<body><table><thead><th>Identity</th>",
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
			self.wfile.write(b"</tbody></table></body></html>")
		elif self.path == "/style.css":
			self.send_response_only(http.HTTPStatus.OK, "OK")
			self.send_header("CONTENT-TYPE", "text/css")
			self.end_headers()
			self.wfile.write(
				b"table {"
					b"width:100%"
				b"}"
				b"td {"
					b"border:solid thin;"
					b"text-align:center"
				b"}"
			)
		else:
			self.send_error(404)
	def do_POST(self):
		self.log_request()
		if self.path == "/":
			body = self.content()
			queries = {k: v[0] for k, v in urllib.parse.parse_qs(body.decode("UTF-8")).items()}
			print(queries)
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