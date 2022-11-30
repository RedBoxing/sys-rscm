import botbase

client = botbase.BotBase("192.168.1.92", 6060)

client.sendCommand("forceOpenCheatProcess")

data = b""
size = -1
while True:
    if size == -1 and len(data) >= 8:
        size = int.from_bytes(data[:8], byteorder="little")
        data = data[8:]

    byte = client.socket.recv(1)
    data += byte
    if byte != b'':
        print(data)
