#include "SilentG.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <algorithm>
#include <wiringPi.h>
#include <argp.h>
//-----------------------------------------------------------------------------
const int c_TransmitPin = 7;
const int c_ReceivePin = 2;
//-----------------------------------------------------------------------------
template <typename T> const T & clamp(const T &v, const T& lo, const T& hi)
{
    return std::min(hi, std::max(lo, v));
}
//-----------------------------------------------------------------------------
static char doc[] = "curtains -- a program to control Silent Gliss Auto Glide 5100 Curtains";
//-----------------------------------------------------------------------------
static argp_option options[] = {
    { 0,0,0,0, "Learn / Read transmitter codes:" },
    { "learn",    'l', 0,      0,  "" },
    { 0,0,0,0, "Transmit code:" },
    { "send",     's', "CODE", 0,  "Transmit code" },
    { "count",    'c', "COUNT", 0, "Number of times to transmit code - default 4" },
    { 0,0,0,0, "Global options:" },
    { "verbose",  'v', 0,      0,  "Produce verbose output" },
    { 0 }
};
//-----------------------------------------------------------------------------
struct Params
{
    bool m_Verbose {false};
    char m_Mode {};
    uint64_t m_Code;
    uint8_t m_Count{ 4 };
} g_Params;
//-----------------------------------------------------------------------------
static error_t parse_opt(int key, char *arg, argp_state *state)
{
    Params *params = reinterpret_cast<Params*>(state->input);

    switch (key)
    {
    case 'v': 
        params->m_Verbose = true;
        break;
    case 'c':
    {
        char * argEnd;
        int argVal = strtol(arg, &argEnd, 10);
        if (argEnd <= arg || argEnd[0] != '\0')
        {
            argp_usage(state);
            return ARGP_KEY_ERROR;
        }
        params->m_Count = static_cast<uint8_t>(clamp(argVal, 0, 255));
        break;
    }
    case 'l':
        params->m_Mode = 'l';
        break;

    case 's':
    {
        params->m_Mode = 's';

        char * argEnd;
        params->m_Code = strtoull(arg, &argEnd, 16);
        if (argEnd <= arg || argEnd[0] != '\0')
        {
            argp_usage(state);
            return ARGP_KEY_ERROR;
        }
        break;
    }

    case ARGP_KEY_END:
        if (params->m_Mode == '\0')
        {
            argp_usage(state);
        }
        break;

    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}
//-----------------------------------------------------------------------------
static argp argp = { options, parse_opt, nullptr, doc };
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int Learn()
{
    RadioInterface rf;

    rf.EnableReceive(c_ReceivePin);  // Receiver on interrupt 0 => that is pin #2
    auto & receiver = rf.GetReceiveBuffer();

    if (g_Params.m_Verbose)
    {
        printf("Listening for button presses ...(verbose)\n");

        while (1)
        {
            const AtomicBuffers::Buffer * const buffer = receiver.StartReadBuffer();
            if (buffer != nullptr)
            {
                printf(" ");

                size_t i = 64;
                for (; i>buffer->m_Size; i-=4)
                {
                    printf("?");
                }

                for (; i>0; i-=4)
                { 
                    int val = 0;
                    for (size_t j = 0; j<4; ++j)
                    {
                        int bi = i + j - 4;
                        if (buffer->m_Bits[bi])
                        {
                            val |= 1ull << j;
                        }
                    }
                    printf("%X", val);
                }
                printf(" : %d / 64 bits\n", buffer->m_Size);
            }
            receiver.EndReadBuffer(buffer);
        }
    }
    else
    {
        printf("Listening for button presses ...\n");

        while (1)
        {
            const size_t c_CodeQty = 3;
            uint64_t codes[c_CodeQty];

            size_t cIdx = 0;
            while (cIdx < c_CodeQty)
            {
                const AtomicBuffers::Buffer * const buffer = receiver.StartReadBuffer();
                if (buffer != nullptr && buffer->m_Size == 64)
                {
                    uint64_t val = 0;
                    for (size_t i = buffer->m_Size; i != 0; --i)
                    {
                        int bi = i - 1;
                        if (buffer->m_Bits[bi])
                        {
                            val |= 1ull << bi;
                        }
                    }

                    codes[cIdx] = val;
                    ++cIdx;
                }
                receiver.EndReadBuffer(buffer);
            }

            bool ok = true;
            for (cIdx = 1; cIdx < c_CodeQty; ++cIdx)
            {
                ok &= (codes[0] == codes[cIdx]);
            }
            if (ok)
            {
                printf(" 0x%016llX\n", codes[0]);
            }
        }
    }
    return 0;
}
//-----------------------------------------------------------------------------
int Send()
{
    RadioInterface rf;

    rf.EnableTransmit(c_TransmitPin);  // Receiver on interrupt 0 => that is pin #2

    if (g_Params.m_Verbose)
    {
        printf("Sending 0x%016llX - %d repeats\n", g_Params.m_Code, g_Params.m_Count);
    }

    rf.TransmitCode(g_Params.m_Code, g_Params.m_Count);

    return 0;
}
//-----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    argp_parse(&argp, argc, argv, 0, 0, &g_Params);

    if (wiringPiSetup() == -1)
    {
        printf("wiringPiSetup failed, exiting...");
        return 0;
    }

    switch (g_Params.m_Mode)
    {
    case 'l':
        return Learn();

    case's':
        return Send();
    }

    return 0;
}
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
