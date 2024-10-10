import config

import sqlite3

database = sqlite3.connect(config.database)

cursor = database.cursor()

try:
	print("Create data table")
	cursor.execute('''
CREATE TABLE data (
	campaign TEXT NOT NULL,
	organisation TEXT NOT NULL,
	device TEXT NOT NULL,
	time DATETIME NOT NULL,
	device_time DATETIME,
	clock_synchronized BOOLEAN,
	temperature REAL,
	pressure REAL,
	humidity REAL,
	PRIMARY KEY (campaign, organisation, device, time)
)''')
except Exception as e:
	print(e)
database.commit()

try:
	print("Create position table")
	cursor.execute('''
CREATE TABLE position (
	campaign TEXT NOT NULL,
	organisation TEXT NOT NULL,
	device TEXT NOT NULL,
	time DATETIME NOT NULL,
	browser_time DATETIME,
	position_time DATETIME,
	latitude REAL,
	longitude REAL,
	altitude REAL,
	PRIMARY KEY (campaign, organisation, device, time)
)''')
except Exception as e:
	print(e)
database.commit()

try:
	print("Create password table")
	cursor.execute('''
CREATE TABLE password (
	campaign TEXT NOT NULL,
	password TEXT NOT NULL,
	PRIMARY KEY (campaign)
)''')
except Exception as e:
	print(e)
database.commit()

database.close()
