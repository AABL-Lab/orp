#!/usr/bin/env python
import sqlite3
import rospy
import sys

# print str(sys.argv[0])
conn = sqlite3.connect(str(sys.argv[1]))
c = conn.cursor()

row_names = []
try:
	c.execute('''PRAGMA table_info(items)''')
	for row in c:
		row_names.append(row[1])
except Exception as inst:
	rospy.loginfo("Couldn't get table info: %s", inst)

rospy.init_node('set_params')

try:
	c.execute('''SELECT * FROM items''')
	for row in c:
		for i in xrange(2, len(row_names)):
			if row[i] is not None:
				rospy.set_param('items/'+str(row[1])+'/'+str(row_names[i]), row[i])
except Exception as inst:
	rospy.loginfo("Couldn't load SQLite parameters: %s", inst)
finally:
	rospy.loginfo("Done loading SQLite parameters")