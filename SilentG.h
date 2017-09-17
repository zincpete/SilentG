#ifndef _SILENT_G_H__INCLUDED_
#define _SILENT_G_H__INCLUDED_

#include <stdint.h>
#include <bitset>
#include <atomic>
#include <array>

// Developed and Tested for Silent Gliss AutoGlide 5100 B electric curtains.
//
// 433 MHz remote control protocol
//
// Sync bit followed by 64 data bits
// Sync bit : 5200uS on, 600uS off
// Data bits 0 = 600uS on, 200uS off
// Data bits 1 = 200uS on, 600uS off
//
//     |             Sync               |  Data 1   |  Data 1   |  Data 0   |  Data 0   |
//     |                                |           |           |           |           |
//     | _______________________        | __        | __        | ______    | __        | ___ ...
//      /                       \        /  \        /  \        /      \    /  \        /
//     /                         \______/    \______/    \______/        \__/    \______/
//

//-----------------------------------------------------------------------------
class AtomicBuffers
{
public:
    struct Buffer
    {
        std::bitset<128> m_Bits;
        size_t m_Size = 0;

        void Push(bool one)
        {
            if (m_Size < m_Bits.size())
            {
                m_Bits.set(m_Size, one);
                ++m_Size;
            }
        }
    };

public:
    AtomicBuffers();

    const Buffer * const StartReadBuffer();
    void EndReadBuffer(const Buffer * const buffer);

    Buffer * GetWriteBuffer();
    void EndWriteBuffer(Buffer * buffer);

    uint16_t GetReadWrite() const { return m_ReadWriteIndex.load(); }

private:
    uint8_t Next(uint8_t i) const { return (i + 1) % m_Buffers.size(); }

    using Buffers = std::array<Buffer, 8>;

    Buffers m_Buffers;
    std::atomic<uint16_t> m_ReadWriteIndex;
};

//-----------------------------------------------------------------------------
class RadioInterface
{
public:
    RadioInterface();
    
    void EnableTransmit(int nTransmitterPin);
    void DisableTransmit();
    void TransmitCode(uint64_t code, uint8_t repeats);

    void EnableReceive(int interrupt);
    void EnableReceive();
    void DisableReceive();

    AtomicBuffers & GetReceiveBuffer() { return m_Buffer; }
    uint32_t GetLastDiff() const { return m_LastDiff; }

    struct HighLow
    {
        uint32_t high;
        uint32_t low;
    };

    struct Protocol
    {
        HighLow sync;
        HighLow zero;
        HighLow one;
    };

    struct State
    {
        enum Enum
        {
            WaitingForSync,
            WaitingForHigh,
            WaitingForLowSync,
            WaitingForLowZero,
            WaitingForLowOne,

            EnumCount,
        };
    };

    State::Enum GetState() const { return m_State; }

private:
    void SendWord(uint32_t word);
    void Transmit(HighLow pulses);
    static void ReceiveInterruptHandler();

private:
    static RadioInterface * s_Instance;

    AtomicBuffers m_Buffer;
    Protocol m_Protocol;
    Protocol m_MinProtocol;
    Protocol m_MaxProtocol;
    volatile State::Enum m_State;
    int m_ReceiverInterrupt;
    int m_TransmitterPin;
    volatile uint32_t m_LastDiff;
};
//-----------------------------------------------------------------------------
#endif
