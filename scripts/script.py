import botbase
import binascii

client = botbase.BotBase("192.168.1.92", 6060)
client.forceOpenCheatProcess()

titleID = client.getTitleID()
buildID = client.getBuildID()
mainBase = client.getMainBase()
mainSize = client.getMainSize()
heapBase = client.getHeapBase()
heapSize = client.getHeapSize()


print("Title ID: " + str(hex(int.from_bytes(titleID, "little"))))
print("Build ID: " + str(hex(int.from_bytes(buildID, "little"))))
print("Main Base: " + str(hex(int.from_bytes(mainBase, "little"))))
print("Main Size: " + str(hex(int.from_bytes(mainSize, "little"))))
print("Heap Base: " + str(hex(int.from_bytes(heapBase, "little"))))
print("Heap Size: " + str(hex(int.from_bytes(heapSize, "little"))))

mainBytes = client.readMemory(0, int.from_bytes(
    mainSize, "little"), botbase.MemorySection.MAIN)
#mainBytes = client.dumpMemory(botbase.MemorySection.MAIN)

with open("main.bin", "wb") as f:
    f.write(mainBytes)

print("saved main.bin")
