import socket

from enum import Enum


class MemorySection(Enum):
    HEAP = 0,
    MAIN = 1,
    ABSOLUTE = 2


class BotBase:
    def __init__(self, ip, port):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.connect((ip, port))

    def sendCommand(self, content):
        print("sending: " + content)
        content += '\r\n'  # important for the parser on the switch side
        self.socket.sendall(content.encode())
        return self.readData()

    def readData(self):
        size = -1
        data = b""
        while size == -1 or len(data) != size:
            if size == -1 and len(data) >= 8:
                size = int.from_bytes(data[:8], byteorder="little")
                data = data[8:]
            data += self.socket.recv(1)
        return data

    def forceOpenCheatProcess(self):
        return self.sendCommand("forceOpenCheatProcess")

    def readMemory(self, address, size, memorySection=MemorySection.ABSOLUTE):
        return self.sendCommand(
            "read " + memorySection.name.lower() + f" {address} {size}")

    def writeMemory(self, address, size, data, memorySection=MemorySection.ABSOLUTE):
        return self.sendCommand(
            "write " + memorySection.name.lower() + f" {address} {size} {data}")

    def getTitleID(self):
        return self.sendCommand("getTitleID")

    def getMainBase(self):
        return self.sendCommand("getMainBase")

    def getMainSize(self):
        return self.sendCommand("getMainSize")

    def getBuildID(self):
        return self.sendCommand("getBuildID")

    def getHeapBase(self):
        return self.sendCommand("getHeapBase")

    def getHeapSize(self):
        return self.sendCommand("getHeapSize")

    def dumpMemory(self, memorySection=MemorySection.MAIN):
        if memorySection == MemorySection.ABSOLUTE:
            raise Exception("Absolute memory dump not supported")

        return self.sendCommand("dump " + memorySection.name.lower())
