#pragma once

/**
 * A firy source that serves an already-decompressed image out of RAM
 * instead of reading a local file. Used to hand a decompressed DMS
 * track set to firy as if it were a plain ADF, without ever touching disk.
 */
class cMemorySource : public firy::sources::cInterface {

public:
    cMemorySource(std::vector<uint8_t> pData)
        : firy::sources::cInterface(cChunkSize) {
        mSourceSize = pData.size();
        mCreating = false;
        mSourceID = "dms-memory";

        mBuffer = std::make_shared<firy::tBuffer>();
        if (!pData.empty())
            mBuffer->pushBuffer(pData.data(), pData.size());
    }

    virtual bool create(const std::string pFile) override { return false; }
    virtual bool open(const std::string pID) override { return false; }
    virtual void close() override {}
    virtual bool save(std::string pID = "") override { return true; }

    virtual firy::spBuffer chunk(const size_t pFileOffset = 0) override {
        return mBuffer;
    }

private:
    // Comfortably bigger than any real Amiga floppy (incl. HD), so the whole
    // image always lives in a single chunk at index 0.
    static const size_t cChunkSize = 4 * 1024 * 1024;

    firy::spBuffer mBuffer;
};
