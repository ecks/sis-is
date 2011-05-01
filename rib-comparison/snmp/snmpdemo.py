import netsnmp 

routerIDVar = netsnmp.Varbind('.1.3.6.1.2.1.14.1.1.0') 
sumCheckSumVar = netsnmp.Varbind('.1.3.6.1.2.1.14.1.7.0') 
lsIDVar =     netsnmp.Varbind('.1.3.6.1.2.1.14.12.1.2')
localRIDVar =     netsnmp.Varbind('.1.3.6.1.2.1.14.12.1.3')
sequenceVar =     netsnmp.Varbind('.1.3.6.1.2.1.14.12.1.4')
advertVar =	  netsnmp.Varbind('.1.3.6.1.2.1.14.12.1.7')

#ips = ['10.100.1.19', '10.100.1.20', '10.100.1.21']
ips = ['10.100.1.19', '10.100.1.21']
advertisements = []

for ip in ips:
  rID = netsnmp.snmpget(routerIDVar, Version = 1, DestHost = ip, Community='public') 
  sumCheckSum = netsnmp.snmpget(sumCheckSumVar, Version = 1, DestHost = ip, Community='public') 
  lsID = netsnmp.snmpwalk(lsIDVar, Version = 1, DestHost = ip, Community='public') 
  localRID = netsnmp.snmpwalk(localRIDVar, Version = 1, DestHost = ip, Community='public') 
  sequence = netsnmp.snmpwalk(sequenceVar, Version = 1, DestHost = ip, Community='public') 
  advertisement = netsnmp.snmpwalk(advertVar, Version = 1, DestHost = ip, Community='public') 
  advertisements.append(advertisement)
  print "Router ID: ", rID
#  print "Sum of Checsums: ", sumCheckSum
#  print "LS IDs: ", lsID
#  print "localRID: ", localRID
#  print "sequence: ", sequence
  print advertisement

i = min(len(advertisements[0]), len(advertisements[1]))
same = []
for j in xrange(i):
  same.append(advertisements[0][j] == advertisements[1][j])
  print "advertisement[0][%d]: %s" % (j, advertisements[0][j])
  print "advertisement[1][%d]: %s" % (j, advertisements[1][j])

print same
