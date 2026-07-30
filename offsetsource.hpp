#pragma once

/**
 * A firy source that presents a byte-range window into a real local file -
 * used to hand firy just one partition of an RDB-partitioned .hdf, without
 * loading the (potentially huge) whole hard-disk image into RAM the way
 * cMemorySource does for DMS.
 */
class cOffsetSource : public firy::sources::cInterface {

public:
    cOffsetSource(const std::string& pFilePath, size_t pOffset, size_t pSize)
        : firy::sources::cInterface() {
        mFilePath = pFilePath;
        mBaseOffset = pOffset;
        mSourceSize = pSize;
        mCreating = false;
        mSourceID = pFilePath + "#" + std::to_string(pOffset);
    }

    virtual bool create(const std::string pFile) override { return false; }
    virtual bool open(const std::string pID) override { return false; }
    virtual void close() override { mBuffers.clear(); }

    virtual bool save(std::string pID = "") override {
        for (auto& entry : mBuffers) {
            if (!entry.second->isDirty())
                continue;

            size_t offset = mBaseOffset + entry.first * mSourceChunkSize;
            if (!firy::gResources->FileWrite(mFilePath, offset, entry.second))
                return false;
        }
        return true;
    }

    virtual firy::spBuffer chunk(const size_t pFileOffset = 0) override {
        size_t index = pFileOffset / mSourceChunkSize;

        auto it = mBuffers.find(index);
        if (it != mBuffers.end())
            return it->second;

        size_t chunkStart = index * mSourceChunkSize;
        if (chunkStart >= mSourceSize)
            return 0;

        size_t chunkSize = mSourceChunkSize;
        if (chunkStart + chunkSize > mSourceSize)
            chunkSize = mSourceSize - chunkStart;

        auto data = firy::gResources->FileRead(mFilePath, mBaseOffset + chunkStart, chunkSize);
        if (!data)
            return 0;

        mBuffers.insert({ index, data });
        return data;
    }

private:
    std::string mFilePath;
    size_t mBaseOffset;
};
