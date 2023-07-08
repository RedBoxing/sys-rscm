#pragma once

#include <stdlib.h>
#include <string.h>
#include <cstdarg>
#include <cstdio>

class Buffer
{
public:
    Buffer(size_t size)
    {
        this->buffer = (char *)malloc(size);
        this->buffer_size = size;
        this->write_offset = 0;
        this->read_offset = 0;
    }

    Buffer(char *buffer, size_t size)
    {
        this->buffer = buffer;
        this->buffer_size = size;
        this->write_offset = 0;
        this->read_offset = 0;
    }

    ~Buffer()
    {
        free(this->buffer);
    }

    char *getBuffer()
    {
        return this->buffer;
    }

    size_t getWriteOffset()
    {
        return this->write_offset;
    }

    size_t getReadOffset()
    {
        return this->read_offset;
    }

    void setWriteOffset(size_t offset)
    {
        this->write_offset = offset;
    }

    void setReadOffset(size_t offset)
    {
        this->read_offset = offset;
    }

    template <typename T>
    T read(size_t offset, size_t length)
    {
        T value;
        memcpy(&value, this->buffer + offset, length);
        return value;
    }

    template <typename T>
    T read()
    {
        T value;
        memcpy(&value, this->buffer + this->read_offset, sizeof(T));
        return value;
    }

    template <typename T>
    T read(size_t length)
    {
        T value;
        memcpy(&value, this->buffer + this->read_offset, length);
        this->read_offset += length;
        return value;
    }

    template <typename T>
    T readCompressed(u32 *length)
    {
        u8 compressedFlag = this->read<u8>(sizeof(u8));
        u32 decompressedLength = this->read<u32>(sizeof(u32));

        *length = decompressedLength;

        if (compressedFlag == 0)
        {
            return this->read<T>(decompressedLength);
        }
        else
        {
            u32 compressedLength = this->read<u32>(sizeof(u32));

            T tmp = (T)malloc(compressedLength);

            u8 pos = 0;
            for (int i = 0; i < compressedLength; i += 2)
            {
                u8 value = this->read<u8>(sizeof(u8));
                u8 count = this->read<u8>(sizeof(u8));

                for (int j = 0; j < count; j++)
                {
                    tmp[pos++] = value;
                }
            }

            return tmp;
        }
    }

    char *readString(size_t offset, size_t length)
    {
        char *value = (char *)malloc(length);
        memcpy(value, this->buffer + offset, length);
        return value;
    }

    char *readString(size_t length)
    {
        char *str = this->readString(this->read_offset, length);
        this->read_offset += length;
        return str;
    }

    char *readString()
    {
        size_t length = this->readUnsignedLong();
        return this->readString(length);
    }

    uint64_t readUnsignedLong(size_t offset)
    {
        return this->read<uint64_t>(offset, sizeof(uint64_t));
    }

    uint64_t readUnsignedLong()
    {
        uint64_t value = this->readUnsignedLong(this->read_offset);
        this->read_offset += sizeof(uint64_t);
        return value;
    }

    uint32_t readUnsignedInt(size_t offset)
    {
        return this->read<uint32_t>(offset, sizeof(uint32_t));
    }

    uint32_t readUnsignedInt()
    {
        uint32_t value = this->readUnsignedInt(this->read_offset);
        this->read_offset += sizeof(uint32_t);
        return value;
    }

    uint16_t readUnsignedShort(size_t offset)
    {
        return this->read<uint16_t>(offset, sizeof(uint16_t));
    }

    uint16_t readUnsignedShort()
    {
        uint16_t value = this->readUnsignedShort(this->read_offset);
        this->read_offset += sizeof(uint16_t);
        return value;
    }

    uint8_t readUnsignedByte(size_t offset)
    {
        return this->read<uint8_t>(offset, sizeof(uint8_t));
    }

    uint8_t readUnsignedByte()
    {
        uint8_t value = this->readUnsignedByte(this->read_offset);
        this->read_offset += sizeof(uint8_t);
        return value;
    }

    void write(void *data, size_t offset, size_t length)
    {
        if (this->buffer_size < offset + length)
        {
            this->buffer = (char *)realloc(this->buffer, offset + length);
            this->buffer_size = offset + length;
        }

        memcpy(this->buffer + offset, data, length);
    }

    void write(void *data, size_t length)
    {
        this->write(data, this->write_offset, length);
        this->write_offset += length;
    }

    void writeCompressed(void *data, size_t length)
    {
        static u8 tmp[2048 * 4 * 2];
        u32 pos = 0;

        for (u32 i = 0; i < length; i++)
        {
            u8 value = ((u8 *)data)[i];
            u8 rle = 1;

            while (rle < 255 && i + 1 < length && ((u8 *)data)[i + 1] == value)
            {
                rle++;
                i++;
            }

            tmp[pos++] = value;
            tmp[pos++] = rle;
        }

        u8 compressedFlag = pos > length ? 0 : 1;
        this->write(&compressedFlag, sizeof(u8));
        this->write(&length, sizeof(u32));
        if (!compressedFlag)
        {
            this->write(data, length);
        }
        else
        {
            this->write(&pos, sizeof(u32));
            this->write(tmp, pos);
        }
    }

    void writeString(size_t offset, char *string)
    {
        size_t length = strlen(string);
        this->write(&length, offset, sizeof(size_t));
        this->write(string, offset + sizeof(size_t), length);
    }

    void writeString(char *string, ...)
    {
        char buffer[0x100];

        va_list args;
        va_start(args, string);
        vsnprintf(buffer, sizeof(buffer), string, args);
        va_end(args);

        this->writeString(this->write_offset, buffer);
        this->write_offset += strlen(buffer);
    }

    void writeUnsignedLong(size_t offset, uint64_t value)
    {
        this->write(&value, offset, sizeof(uint64_t));
    }

    void writeUnsignedLong(uint64_t value)
    {
        this->writeUnsignedLong(this->write_offset, value);
        this->write_offset += sizeof(uint64_t);
    }

    void writeUnsignedInt(size_t offset, uint32_t value)
    {
        this->write(&value, offset, sizeof(uint32_t));
    }

    void writeUnsignedInt(uint32_t value)
    {
        this->writeUnsignedInt(this->write_offset, value);
        this->write_offset += sizeof(uint32_t);
    }

    void writeUnsignedShort(size_t offset, uint16_t value)
    {
        this->write(&value, offset, sizeof(uint16_t));
    }

    void writeUnsignedShort(uint16_t value)
    {
        this->writeUnsignedShort(this->write_offset, value);
        this->write_offset += sizeof(uint16_t);
    }

    void writeUnsignedByte(size_t offset, uint8_t value)
    {
        this->write(&value, offset, sizeof(uint8_t));
    }

    void writeUnsignedByte(uint8_t value)
    {
        this->writeUnsignedByte(this->write_offset, value);
        this->write_offset += sizeof(uint8_t);
    }

    void writeBoolean(size_t offset, bool value)
    {
        this->writeUnsignedByte(offset, value ? 1 : 0);
    }

    void reallocate(size_t size)
    {
        this->buffer = (char *)realloc(this->buffer, size);
        this->buffer_size = size;
    }

    // offset the content of the buffer by the specified amount of bytes
    void offset(size_t offset)
    {
        if (offset == 0)
            return;

        this->buffer = (char *)realloc(this->buffer, this->buffer_size + offset);
        this->buffer_size += offset;

        memmove(this->buffer + offset, this->buffer, this->buffer_size - offset);

        this->write_offset += offset;
        this->read_offset += offset;
    }

private:
    char *buffer;
    size_t buffer_size;
    size_t write_offset;
    size_t read_offset;
};