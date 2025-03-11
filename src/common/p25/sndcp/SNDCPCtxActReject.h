// SPDX-License-Identifier: GPL-2.0-only
/*
 * Digital Voice Modem - Common Library
 * GPLv2 Open Source. Use is subject to license terms.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 *  Copyright (C) 2024 Bryan Biedenkapp, N2PLL
 *
 */
/**
 * @file SNDCPCtxActReject.h
 * @ingroup p25_sndcp
 * @file SNDCPCtxActReject.cpp
 * @ingroup p25_sndcp
 */
#if !defined(__P25_SNDCP__SNDCPCTXACTREJECT_H__)
#define  __P25_SNDCP__SNDCPCTXACTREJECT_H__

#include "common/Defines.h"
#include "common/p25/sndcp/SNDCPPacket.h"
#include "common/Utils.h"

#include <string>

namespace p25
{
    namespace sndcp
    {
        // ---------------------------------------------------------------------------
        //  Class Declaration
        // ---------------------------------------------------------------------------

        /**
         * @brief Represents a SNDCP PDU context activation reject response.
         * @ingroup p25_sndcp
         */
        class HOST_SW_API SNDCPCtxActReject : public SNDCPPacket {
        public:
            /**
             * @brief Initializes a new instance of the SNDCPCtxActReject class.
             */
            SNDCPCtxActReject();

            /**
             * @brief Decode a SNDCP context activation reject packet.
             * @param[in] data Buffer containing SNDCP packet data to decode.
             * @returns bool True, if decoded, otherwise false.
             */
            bool decode(const uint8_t* data) override;
            /**
             * @brief Encode a SNDCP context activation reject packet.
             * @param[out] data Buffer to encode SNDCP packet data to.
             */
            void encode(uint8_t* data) override;

        public:
            /**
             * @brief Reject Code
             */
            __PROPERTY(uint8_t, rejectCode, RejectCode);

            __COPY(SNDCPCtxActReject);
        };
    } // namespace sndcp
} // namespace p25

#endif // __P25_SNDCP__SNDCPCTXACTREJECT_H__
