#include "SilentG.h"

#include <string.h>
#include <stdlib.h>
#include <wiringPi.h>
#include <stdio.h>

//-----------------------------------------------------------------------------
AtomicBuffers::AtomicBuffers()
{
    m_ReadWriteIndex.store(0);
}
//-----------------------------------------------------------------------------
const AtomicBuffers::Buffer * const AtomicBuffers::StartReadBuffer()
{
    uint16_t readWrite = m_ReadWriteIndex.load();
    uint8_t read = readWrite & 0xff;
    uint8_t write = readWrite >> 8;

    if (read == write)
    {
        // empty
        return nullptr;
    }
    else
    {
        return &m_Buffers[read];
    }
}
//-----------------------------------------------------------------------------
void AtomicBuffers::EndReadBuffer(const AtomicBuffers::Buffer * const buffer)
{
    if (buffer != nullptr)
    {
        uint16_t oldRW, newRW;
        do
        {
            oldRW = m_ReadWriteIndex.load();
            m_Buffers[oldRW & 0xff].m_Size = 0;
            newRW = (oldRW & 0xff00) | Next(oldRW & 0xff);
        } while (!m_ReadWriteIndex.compare_exchange_strong(oldRW, newRW));
    }
}
//-----------------------------------------------------------------------------
AtomicBuffers::Buffer * AtomicBuffers::GetWriteBuffer()
{
    uint16_t readWrite = m_ReadWriteIndex.load();
    uint8_t read = readWrite & 0xff;
    uint8_t write = readWrite >> 8;

    if (read == Next(write))
    {   // full
        return nullptr;
    }
    else
    {
        return &m_Buffers[write];
    }
}
//-----------------------------------------------------------------------------
void AtomicBuffers::EndWriteBuffer(AtomicBuffers::Buffer * buffer)
{
    if (buffer != nullptr && buffer->m_Size > 0)
    {
        uint16_t oldRW, newRW;
        do
        {
            oldRW = m_ReadWriteIndex.load();
            newRW = (oldRW & 0x00ff) | (Next((oldRW>>8) & 0xff) << 8);
        } while (!m_ReadWriteIndex.compare_exchange_strong(oldRW, newRW));
    }
}
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void RadioInterface::Protocol::Set(const Protocol &protocol, int tolerance)
{
    sync.high = protocol.sync.high + tolerance;
    sync.low = protocol.sync.low + tolerance;
    zero.high = protocol.zero.high + tolerance;
    zero.low  = protocol.zero.low + tolerance;
    one.high = protocol.one.high + tolerance;
    one.low = protocol.one.low + tolerance;
}
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
RadioInterface * RadioInterface::s_Instance = nullptr;
//-----------------------------------------------------------------------------
RadioInterface::RadioInterface()
{
    m_TransmitterPin = -1;
    m_ReceiverInterrupt = -1;

    m_Protocol = { { 5200, 600 }, { 600, 200 }, { 200, 600 } };
    m_MinProtocol = { { 5000, 500 }, { 500, 150 }, { 150, 500 } };
    m_MaxProtocol = { { 5400, 700 }, { 700, 350 }, { 350, 700 } };
    m_LastDiff = 0xffffffff;
    m_State = State::WaitingForHigh;

    s_Instance = this;
}
//-----------------------------------------------------------------------------
void RadioInterface::EnableTransmit(int nTransmitterPin)
{
    this->m_TransmitterPin = nTransmitterPin;
    pinMode(this->m_TransmitterPin, OUTPUT);
}
//-----------------------------------------------------------------------------
void RadioInterface::DisableTransmit()
{
    m_TransmitterPin = -1;
}
//-----------------------------------------------------------------------------
/* Transmit a single high-low pulse. */
void RadioInterface::Transmit(HighLow pulses)
{
    digitalWrite(m_TransmitterPin, HIGH);
    delayMicroseconds(pulses.high);
    digitalWrite(m_TransmitterPin, LOW);
    delayMicroseconds(pulses.low);
}
//-----------------------------------------------------------------------------
void RadioInterface::SendWord(uint32_t word)
{
    for (int i = 32; i; --i)
    {
        if ((word & 0x1) != 0)
        {
            Transmit(m_Protocol.one);
        }
        else
        {
            Transmit(m_Protocol.zero);
        }
        word >>= 1;
    }
}
//-----------------------------------------------------------------------------
void RadioInterface::TransmitCode(uint64_t code, uint8_t repeats)
{
    while (repeats > 0)
    {
        Transmit(m_Protocol.sync);
        SendWord(static_cast<uint32_t>(code));
        SendWord(static_cast<uint32_t>(code >> 32));
        --repeats;
    }
}
//-----------------------------------------------------------------------------
void RadioInterface::EnableReceive(int interrupt)
{
    m_ReceiverInterrupt = interrupt;
    EnableReceive();
}
//-----------------------------------------------------------------------------
void RadioInterface::EnableReceive()
{
    if (m_ReceiverInterrupt != -1)
    {
        pinMode(m_ReceiverInterrupt, INPUT);
        pullUpDnControl(m_ReceiverInterrupt, PUD_UP);

        printf("Enable Receive interrupt on pin %d\n", m_ReceiverInterrupt);

        wiringPiISR(m_ReceiverInterrupt, INT_EDGE_BOTH, &ReceiveInterruptHandler);
    }
}
//-----------------------------------------------------------------------------
void RadioInterface::DisableReceive()
{
    printf("DisableReceive\n");
    m_ReceiverInterrupt = -1;
}
//-----------------------------------------------------------------------------
static bool InRange(uint32_t min, uint32_t duration, uint32_t max)
{
    return ((min<duration) && (duration<max));
}
//-----------------------------------------------------------------------------
void RadioInterface::ReceiveInterruptHandler()
{
    static uint32_t s_lastTime = 0;

    const uint32_t time = micros();
    const uint32_t duration = time - s_lastTime;

    s_Instance->m_LastDiff = duration;
    s_lastTime = time;

    auto * buffer = s_Instance->m_Buffer.GetWriteBuffer();

    if (buffer == nullptr)
    {
        s_Instance->m_State = State::WaitingForSync;
    }
    else
    {
        if (InRange(s_Instance->m_MinProtocol.sync.high, duration, s_Instance->m_MaxProtocol.sync.high))
        {
            s_Instance->m_Buffer.EndWriteBuffer(buffer);
            s_Instance->m_State = State::WaitingForLowSync;
        }
        else
        {
            switch (s_Instance->m_State)
            {
            case State::WaitingForSync:
                break;
            case State::WaitingForLowSync:
                if (InRange(s_Instance->m_MinProtocol.sync.low, duration, s_Instance->m_MaxProtocol.sync.low))
                {
                    s_Instance->m_State = State::WaitingForHigh;
                }
                else
                {
                    s_Instance->m_State = State::WaitingForSync;
                }
                break;
            case State::WaitingForHigh:
                if (InRange(s_Instance->m_MinProtocol.zero.high, duration, s_Instance->m_MaxProtocol.zero.high))
                {
                    s_Instance->m_State = State::WaitingForLowZero;
                }
                else if (InRange(s_Instance->m_MinProtocol.one.high, duration, s_Instance->m_MaxProtocol.one.high))
                {
                    s_Instance->m_State = State::WaitingForLowOne;
                }
                else
                {
                    s_Instance->m_State = State::WaitingForSync;
                }
                break;
            case State::WaitingForLowZero:
                if (InRange(s_Instance->m_MinProtocol.zero.low, duration, s_Instance->m_MaxProtocol.zero.low))
                {
                    buffer->Push(false);
                    s_Instance->m_State = State::WaitingForHigh;
                }
                else
                {
                    s_Instance->m_State = State::WaitingForSync;
                }
                break;
            case State::WaitingForLowOne:
                if (InRange(s_Instance->m_MinProtocol.one.low, duration, s_Instance->m_MaxProtocol.one.low))
                {
                    buffer->Push(true);
                    s_Instance->m_State = State::WaitingForHigh;
                }
                else
                {
                    s_Instance->m_State = State::WaitingForSync;
                }
                break;
            }
        }
    }
}
//-----------------------------------------------------------------------------
