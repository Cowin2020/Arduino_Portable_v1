import config

import sqlite3

database = sqlite3.connect(config.database)

cursor = database.cursor()

try:
	print("Create data table")
	cursor.execute('''
CREATE TABLE data (
	organisation TEXT NOT NULL,
	campaign TEXT NOT NULL,
	device TEXT NOT NULL,
	time DATETIME NOT NULL,
	device_time DATETIME,
	temperature REAL,
	humidity REAL,
	PRIMARY KEY (organisation, campaign, device, time)
)''')
except Exception as e:
	print(e)
database.commit()

try:
	print("Create position table")
	cursor.execute('''
CREATE TABLE position (
	organisation TEXT NOT NULL,
	campaign TEXT NOT NULL,
	device TEXT NOT NULL,
	time DATETIME NOT NULL,
	browser_time DATETIME,
	position_time DATETIME,
	latitude REAL,
	longitude REAL,
	altitude REAL,
	PRIMARY KEY (organisation, campaign, device, time)
)''')
except Exception as e:
	print(e)
database.commit()

try:
	print("Create password table")
	cursor.execute('''
CREATE TABLE password (
	organisation TEXT NOT NULL,
	password TEXT NOT NULL,
	PRIMARY KEY (organisation)
)''')
except Exception as e:
	print(e)
database.commit()

database.close()
