import config

import sqlite3

database = sqlite3.connect(config.database)

cursor = database.cursor()

try:
	print("Create data table")
	cursor.execute('''
CREATE TABLE data (
	device TEXT NOT NULL,
	time DATETIME NOT NULL,
	temperature REAL,
	humidity REAL,
	PRIMARY KEY (device, time)
)''')
except Exception as e:
	print(e)
database.commit()

try:
	print("Create position table")
	cursor.execute('''
CREATE TABLE position (
	device TEXT NOT NULL,
	time DATETIME NOT NULL,
	latitude REAL,
	longitude REAL,
	altitude REAL,
	PRIMARY KEY (device, time)
)''')
except Exception as e:
	print(e)
database.commit()

database.close()
