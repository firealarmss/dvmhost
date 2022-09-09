/**
* Digital Voice Modem - Host Software
* GPLv2 Open Source. Use is subject to license terms.
* DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
*
* @package DVM / Host Software
*
*/
//
// Based on code from the MMDVMHost project. (https://github.com/g4klx/MMDVMHost)
// Licensed under the GPLv2 License (https://opensource.org/licenses/GPL-2.0)
//
/*
*   Copyright (C) 2022 by Bryan Biedenkapp N2PLL
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program; if not, write to the Free Software
*   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
#include "Defines.h"
#include "nxdn/NXDNDefines.h"
#include "nxdn/channel/CAC.h"
#include "nxdn/packet/Trunk.h"
#include "nxdn/acl/AccessControl.h"
#include "nxdn/Sync.h"
#include "edac/CRC.h"
#include "HostMain.h"
#include "Log.h"
#include "Utils.h"

using namespace nxdn;
using namespace nxdn::packet;

#include <cassert>
#include <cstdio>
#include <cstring>
#include <ctime>

// ---------------------------------------------------------------------------
//  Macros
// ---------------------------------------------------------------------------

// Make sure control data is supported.
#define IS_SUPPORT_CONTROL_CHECK(_PCKT_STR, _PCKT, _SRCID)                              \
    if (!m_nxdn->m_control) {                                                           \
        LogWarning(LOG_RF, "NXDN, " _PCKT_STR " denial, unsupported service, srcId = %u", _SRCID); \
        writeRF_Message_Deny(NXDN_CAUSE_SVC_UNAVAILABLE, _PCKT);                        \
        m_nxdn->m_rfState = RS_RF_REJECTED;                                             \
        return false;                                                                   \
    }

// Validate the source RID.
#define VALID_SRCID(_PCKT_STR, _PCKT, _SRCID, _RSN)                                      \
    if (!acl::AccessControl::validateSrcId(_SRCID)) {                                   \
        LogWarning(LOG_RF, "NXDN, " _PCKT_STR " denial, RID rejection, srcId = %u", _SRCID); \
        writeRF_Message_Deny(_RSN, _PCKT);                                              \
        m_nxdn->m_rfState = RS_RF_REJECTED;                                             \
        return false;                                                                   \
    }

// Validate the target RID.
#define VALID_DSTID(_PCKT_STR, _PCKT, _DSTID, _RSN)                                     \
    if (!acl::AccessControl::validateSrcId(_DSTID)) {                                   \
        LogWarning(LOG_RF, "NXDN, " _PCKT_STR " denial, RID rejection, dstId = %u", _DSTID); \
        writeRF_Message_Deny(_RSN, _PCKT);                                              \
        m_nxdn->m_rfState = RS_RF_REJECTED;                                             \
        return false;                                                                   \
    }

// Validate the talkgroup ID.
#define VALID_TGID(_PCKT_STR, _PCKT, _DSTID, _RSN)                                      \
    if (!acl::AccessControl::validateTGId(_DSTID)) {                                    \
        LogWarning(LOG_RF, "NXDN, " _PCKT_STR " denial, TGID rejection, dstId = %u", _DSTID); \
        writeRF_Message_Deny(_RSN, _PCKT);                                              \
        m_nxdn->m_rfState = RS_RF_REJECTED;                                             \
        return false;                                                                   \
    }

// Verify the source RID is registered.
#define VERIFY_SRCID_REG(_PCKT_STR, _PCKT, _SRCID, _RSN)                                \
    if (!m_nxdn->m_affiliations.isUnitReg(_SRCID) && m_verifyReg) {                     \
        LogWarning(LOG_RF, "NXDN, " _PCKT_STR " denial, RID not registered, srcId = %u", _SRCID); \
        writeRF_Message_Deny(_RSN, _PCKT);                                              \
        m_nxdn->m_rfState = RS_RF_REJECTED;                                             \
        return false;                                                                   \
    }

// Verify the source RID is affiliated.
#define VERIFY_SRCID_AFF(_PCKT_STR, _PCKT, _SRCID, _DSTID, _RSN)                        \
    if (!m_nxdn->m_affiliations.isGroupAff(_SRCID, _DSTID) && m_verifyAff) {            \
        LogWarning(LOG_RF, "NXDN, " _PCKT_STR " denial, RID not affiliated to TGID, srcId = %u, dstId = %u", _SRCID, _DSTID); \
        writeRF_Message_Deny(_RSN, _PCKT);                                              \
        m_nxdn->m_rfState = RS_RF_REJECTED;                                             \
        return false;                                                                   \
    }

// ---------------------------------------------------------------------------
//  Constants
// ---------------------------------------------------------------------------

const uint32_t GRANT_TIMER_TIMEOUT = 15U;

// ---------------------------------------------------------------------------
//  Public Class Members
// ---------------------------------------------------------------------------

/// <summary>
/// Resets the data states for the RF interface.
/// </summary>
void Trunk::resetRF()
{
    lc::RCCH lc = lc::RCCH(m_nxdn->m_siteData, m_nxdn->m_idenEntry, m_dumpRCCH);
    m_rfLC = lc;
}

/// <summary>
/// Resets the data states for the network.
/// </summary>
void Trunk::resetNet()
{
    lc::RCCH lc = lc::RCCH(m_nxdn->m_siteData, m_nxdn->m_idenEntry, m_dumpRCCH);
    m_netLC = lc;
}

/// <summary>
/// Process a data frame from the RF interface.
/// </summary>
/// <param name="fct">Functional channel type.</param>
/// <param name="option">Channel options.</param>
/// <param name="data">Buffer containing data frame.</param>
/// <param name="len">Length of data frame.</param>
/// <returns></returns>
bool Trunk::process(uint8_t fct, uint8_t option, uint8_t* data, uint32_t len)
{
    assert(data != NULL);

    channel::CAC cac;
    bool validCAC = cac.decode(data + 2U);
    if (m_nxdn->m_rfState == RS_RF_LISTENING && !validCAC)
        return false;

    if (validCAC) {
        uint8_t ran = cac.getRAN();
        if (ran != m_nxdn->m_ran && ran != 0U)
            return false;
    }

    RPT_RF_STATE prevRfState = m_nxdn->m_rfState;
    if (m_nxdn->m_rfState != RS_RF_DATA) {
        m_nxdn->m_rfState = RS_RF_DATA;
    }

    m_nxdn->m_queue.clear();

    // the layer3 data will only be correct if valid is true
    uint8_t buffer[NXDN_FRAME_LENGTH_BYTES];
    cac.getData(buffer);

    m_rfLC.decode(buffer, NXDN_RCCH_CAC_LC_SHORT_LENGTH_BITS);

    uint16_t srcId = m_rfLC.getSrcId();
    uint16_t dstId = m_rfLC.getDstId();

    switch (m_rfLC.getMessageType()) {
        case RTCH_MESSAGE_TYPE_VCALL:
            // make sure control data is supported
            IS_SUPPORT_CONTROL_CHECK(NXDN_RTCH_MSG_TYPE_VCALL_REQ, RTCH_MESSAGE_TYPE_VCALL, srcId);

            // validate the source RID
            VALID_SRCID(NXDN_RTCH_MSG_TYPE_VCALL_REQ, RTCH_MESSAGE_TYPE_VCALL, srcId, NXDN_CAUSE_VD_REQ_UNIT_NOT_PERM);

            // validate the talkgroup ID
            VALID_TGID(NXDN_RTCH_MSG_TYPE_VCALL_REQ, RTCH_MESSAGE_TYPE_VCALL, dstId, NXDN_CAUSE_VD_TGT_UNIT_NOT_PERM);

            // verify the source RID is affiliated
            VERIFY_SRCID_AFF(NXDN_RTCH_MSG_TYPE_VCALL_REQ, RTCH_MESSAGE_TYPE_VCALL, srcId, dstId, NXDN_CAUSE_VD_REQ_UNIT_NOT_REG);

            if (m_verbose) {
                LogMessage(LOG_RF, "NXDN, " NXDN_RTCH_MSG_TYPE_VCALL_REQ ", srcId = %u, dstId = %u", srcId, dstId);
            }

            writeRF_Message_Grant(true);
            break;
        case RCCH_MESSAGE_TYPE_REG:
            // make sure control data is supported
            IS_SUPPORT_CONTROL_CHECK(NXDN_RCCH_MSG_TYPE_REG_REQ, RCCH_MESSAGE_TYPE_REG, srcId);

            if (m_verbose) {
                LogMessage(LOG_RF, "NXDN, " NXDN_RCCH_MSG_TYPE_REG_REQ ", srcId = %u", srcId);
            }

            writeRF_Message_U_Reg_Rsp(srcId);
            break;
        case RCCH_MESSAGE_TYPE_GRP_REG:
            // make sure control data is supported
            IS_SUPPORT_CONTROL_CHECK(NXDN_RCCH_MSG_TYPE_GRP_REG_REQ, RCCH_MESSAGE_TYPE_GRP_REG, srcId);

            if (m_verbose) {
                LogMessage(LOG_RF, "NXDN, " NXDN_RCCH_MSG_TYPE_GRP_REG_REQ ", srcId = %u, dstId = %u", srcId, dstId);
            }

            writeRF_Message_Grp_Reg_Rsp(srcId, dstId);
            break;
        default:
            LogError(LOG_RF, "NXDN, unhandled message type, messageType = $%02X", m_rfLC.getMessageType());
            break;
    }

    m_nxdn->m_rfState = prevRfState;
    return true;
}

/// <summary>
/// Process a data frame from the RF interface.
/// </summary>
/// <param name="fct">Functional channel type.</param>
/// <param name="option">Channel options.</param>
/// <param name="netLC"></param>
/// <param name="data">Buffer containing data frame.</param>
/// <param name="len">Length of data frame.</param>
/// <returns></returns>
bool Trunk::processNetwork(uint8_t fct, uint8_t option, lc::RTCH& netLC, uint8_t* data, uint32_t len)
{
    assert(data != NULL);

    if (m_nxdn->m_netState == RS_NET_IDLE) {
        m_nxdn->m_queue.clear();
        
        resetRF();
        resetNet();
    }

    return true;
}

/// <summary>
/// Updates the processor by the passed number of milliseconds.
/// </summary>
/// <param name="ms"></param>
void Trunk::clock(uint32_t ms)
{
    if (m_nxdn->m_control) {
        if (m_nxdn->m_network != NULL) {
            if (m_nxdn->m_network->isHandlingChGrants() && m_nxdn->m_siteData.netActive()) {
                bool grp = true;
                uint32_t srcId = 0U;
                uint32_t dstId = 0U;
                uint32_t grpVchNo = 0U;

                // do we have a grant response?
                if (m_nxdn->m_network->readGrantRsp(grp, srcId, dstId, grpVchNo)) {
                    m_rfLC.setSrcId(srcId);
                    m_rfLC.setDstId(dstId);
                    m_rfLC.setGrpVchNo(grpVchNo);

                    writeRF_Message_Grant(grp, true, true, true);
                }
            }
        }

        // clock all the grant timers
        m_nxdn->m_affiliations.clock(ms);
    }
}

// ---------------------------------------------------------------------------
//  Protected Class Members
// ---------------------------------------------------------------------------

/// <summary>
/// Initializes a new instance of the Trunk class.
/// </summary>
/// <param name="nxdn">Instance of the Control class.</param>
/// <param name="network">Instance of the BaseNetwork class.</param>
/// <param name="dumpRCCHData">Flag indicating whether RCCH data is dumped to the log.</param>
/// <param name="debug">Flag indicating whether NXDN debug is enabled.</param>
/// <param name="verbose">Flag indicating whether NXDN verbose logging is enabled.</param>
Trunk::Trunk(Control* nxdn, network::BaseNetwork* network, bool dumpRCCHData, bool debug, bool verbose) :
    m_nxdn(nxdn),
    m_network(network),
    m_verifyAff(false),
    m_verifyReg(false),
    m_rfLC(SiteData(), lookups::IdenTable()),
    m_netLC(SiteData(), lookups::IdenTable()),
    m_lastRejectId(0U),
    m_dumpRCCH(dumpRCCHData),
    m_verbose(verbose),
    m_debug(debug)
{
    /* stub */
}

/// <summary>
/// Finalizes a instance of the Trunk class.
/// </summary>
Trunk::~Trunk()
{
    /* stub */
}

/// <summary>
/// Write data processed from RF to the network.
/// </summary>
/// <param name="data"></param>
/// <param name="len"></param>
void Trunk::writeNetwork(const uint8_t *data, uint32_t len)
{
    assert(data != NULL);

    if (m_network == NULL)
        return;

    if (m_nxdn->m_rfTimeout.isRunning() && m_nxdn->m_rfTimeout.hasExpired())
        return;

    m_network->writeNXDN(m_nxdn->m_rfLC, data, len);
}

/// <summary>
/// Helper to write control channel packet data.
/// </summary>
/// <param name="frameCnt"></param>
/// <param name="n"></param>
/// <param name="adjSS"></param>
void Trunk::writeRF_ControlData(uint8_t frameCnt, uint8_t n, bool adjSS)
{
    uint8_t i = 0U, seqCnt = 0U;

    if (!m_nxdn->m_control)
        return;

    // don't add any frames if the queue is full
    uint8_t len = NXDN_FRAME_LENGTH_BYTES + 2U;
    uint32_t space = m_nxdn->m_queue.freeSpace();
    if (space < (len + 1U)) {
        return;
    }

    do
    {
        if (m_debug) {
            LogDebug(LOG_NXDN, "writeRF_ControlData, frameCnt = %u, seq = %u", frameCnt, n);
        }

        switch (n)
        {            
        case 0:
            writeRF_CC_Message_Site_Info();
            break;
        default:
            writeRF_CC_Message_Service_Info();
            break;
        }

        if (seqCnt > 0U)
            n++;
        i++;
    } while (i <= seqCnt);
}

/// <summary>
/// Helper to write a single-block RCCH packet.
/// </summary>
/// <param name="noNetwork"></param>
/// <param name="clearBeforeWrite"></param>
void Trunk::writeRF_Message(bool noNetwork, bool clearBeforeWrite)
{
    if (!m_nxdn->m_control)
        return;

    uint8_t data[NXDN_FRAME_LENGTH_BYTES + 2U];
    ::memset(data + 2U, 0x00U, NXDN_FRAME_LENGTH_BYTES);

    Sync::addNXDNSync(data + 2U);

    // generate the LICH
    channel::LICH lich;
    lich.setRFCT(NXDN_LICH_RFCT_RCCH);
    lich.setFCT(NXDN_LICH_CAC_OUTBOUND);
    lich.setOption(NXDN_LICH_DATA_COMMON);
    lich.setOutbound(true);
    lich.encode(data + 2U);

    uint8_t buffer[NXDN_RCCH_LC_LENGTH_BYTES];
    ::memset(buffer, 0x00U, NXDN_RCCH_LC_LENGTH_BYTES);

    m_rfLC.encode(buffer, NXDN_RCCH_LC_LENGTH_BITS);

    // generate the CAC
    channel::CAC cac;
    cac.setVerbose(m_dumpRCCH);
    cac.setRAN(m_nxdn->m_ran);
    cac.setStructure(NXDN_SR_RCCH_SINGLE);
    cac.setData(buffer);
    cac.encode(data + 2U);

    data[0U] = modem::TAG_DATA;
    data[1U] = 0x00U;

    m_nxdn->scrambler(data + 2U);

    addPostBits(data + 2U);

    if (!noNetwork)
        writeNetwork(data, NXDN_FRAME_LENGTH_BYTES + 2U);

    if (clearBeforeWrite) {
        m_nxdn->m_modem->clearP25Data();
        m_nxdn->m_queue.clear();
    }

    if (m_nxdn->m_duplex) {
        m_nxdn->addFrame(data, NXDN_FRAME_LENGTH_BYTES + 2U);
    }
}

/// <summary>
/// Helper to write a grant packet.
/// </summary>
/// <param name="grp"></param>
/// <param name="skip"></param>
/// <param name="net"></param>
/// <param name="skipNetCheck"></param>
/// <returns></returns>
bool Trunk::writeRF_Message_Grant(bool grp, bool skip, bool net, bool skipNetCheck)
{
    uint8_t messageType = m_rfLC.getMessageType();

    // do we have a network connection and are we handling grants at the network?
    if (m_nxdn->m_network != NULL) {
        if (m_nxdn->m_network->isHandlingChGrants() && m_nxdn->m_siteData.netActive() && !skipNetCheck) {
            return m_nxdn->m_network->writeGrantReq(grp, m_rfLC.getSrcId(), m_rfLC.getDstId());
        }
    }

    // are we skipping checking?
    if (!skip) {
        if (m_nxdn->m_rfState != RS_RF_LISTENING && m_nxdn->m_rfState != RS_RF_DATA) {
            if (!net) {
                LogWarning(LOG_RF, "NXDN, " NXDN_RTCH_MSG_TYPE_VCALL_REQ " denied, traffic in progress, dstId = %u", m_rfLC.getDstId());
                writeRF_Message_Deny(NXDN_CAUSE_VD_QUE_GRP_BUSY, RTCH_MESSAGE_TYPE_VCALL);

                ::ActivityLog("NXDN", true, "group grant request from %u to TG %u denied", m_rfLC.getSrcId(), m_rfLC.getDstId());
                m_nxdn->m_rfState = RS_RF_REJECTED;
            }

            m_rfLC.setMessageType(messageType);
            return false;
        }

        if (m_nxdn->m_netState != RS_NET_IDLE && m_rfLC.getDstId() == m_nxdn->m_netLastDstId) {
            if (!net) {
                LogWarning(LOG_RF, "NXDN, " NXDN_RTCH_MSG_TYPE_VCALL_REQ " denied, traffic in progress, dstId = %u", m_rfLC.getDstId());
                writeRF_Message_Deny(NXDN_CAUSE_VD_QUE_GRP_BUSY, RTCH_MESSAGE_TYPE_VCALL);

                ::ActivityLog("NXDN", true, "group grant request from %u to TG %u denied", m_rfLC.getSrcId(), m_rfLC.getDstId());
                m_nxdn->m_rfState = RS_RF_REJECTED;
            }

            m_rfLC.setMessageType(messageType);
            return false;
        }

        // don't transmit grants if the destination ID's don't match and the network TG hang timer is running
        if (m_nxdn->m_rfLastDstId != 0U) {
            if (m_nxdn->m_rfLastDstId != m_rfLC.getDstId() && (m_nxdn->m_rfTGHang.isRunning() && !m_nxdn->m_rfTGHang.hasExpired())) {
                if (!net) {
                    writeRF_Message_Deny(NXDN_CAUSE_VD_QUE_GRP_BUSY, RTCH_MESSAGE_TYPE_VCALL);
                    m_nxdn->m_rfState = RS_RF_REJECTED;
                }

                m_rfLC.setMessageType(messageType);
                return false;
            }
        }

        if (!m_nxdn->m_affiliations.isGranted(m_rfLC.getDstId())) {
            if (!m_nxdn->m_affiliations.isRFChAvailable()) {
                if (grp) {
                    if (!net) {
                        LogWarning(LOG_RF, "NXDN, " NXDN_RTCH_MSG_TYPE_VCALL_REQ " queued, no channels available, dstId = %u", m_rfLC.getDstId());
                        writeRF_Message_Deny(NXDN_CAUSE_VD_QUE_CHN_RESOURCE_NOT_AVAIL, RTCH_MESSAGE_TYPE_VCALL);

                        ::ActivityLog("NXDN", true, "group grant request from %u to TG %u queued", m_rfLC.getSrcId(), m_rfLC.getDstId());
                        m_nxdn->m_rfState = RS_RF_REJECTED;
                    }

                    m_rfLC.setMessageType(messageType);
                    return false;
                }
                else {
                    if (!net) {
                        LogWarning(LOG_RF, "NXDN, " NXDN_RTCH_MSG_TYPE_VCALL_REQ " queued, no channels available, dstId = %u", m_rfLC.getDstId());
                        writeRF_Message_Deny(NXDN_CAUSE_VD_QUE_CHN_RESOURCE_NOT_AVAIL, RTCH_MESSAGE_TYPE_VCALL);

                        ::ActivityLog("P25", true, "unit-to-unit grant request from %u to %u queued", m_rfLC.getSrcId(), m_rfLC.getDstId());
                        m_nxdn->m_rfState = RS_RF_REJECTED;
                    }

                    m_rfLC.setMessageType(messageType);
                    return false;
                }
            }
            else {
                if (m_nxdn->m_affiliations.grantCh(m_rfLC.getDstId(), GRANT_TIMER_TIMEOUT)) {
                    uint32_t chNo = m_nxdn->m_affiliations.getGrantedCh(m_rfLC.getDstId());
                    m_rfLC.setGrpVchNo(chNo);
                }
            }
        }
        else {
            uint32_t chNo = m_nxdn->m_affiliations.getGrantedCh(m_rfLC.getDstId());
            m_rfLC.setGrpVchNo(chNo);
        }
    }

    if (grp) {
        if (!net) {
            ::ActivityLog("NXDN", true, "group grant request from %u to TG %u", m_rfLC.getSrcId(), m_rfLC.getDstId());
        }
    }
    else {
        if (!net) {
            ::ActivityLog("NXDN", true, "unit-to-unit grant request from %u to %u", m_rfLC.getSrcId(), m_rfLC.getDstId());
        }
    }

    if (m_verbose) {
        LogMessage((net) ? LOG_NET : LOG_RF, "NXDN, " NXDN_RTCH_MSG_TYPE_VCALL_RESP ", emerg = %u, encrypt = %u, prio = %u, chNo = %u, srcId = %u, dstId = %u",
            m_rfLC.getEmergency(), m_rfLC.getEncrypted(), m_rfLC.getPriority(), m_rfLC.getGrpVchNo(), m_rfLC.getSrcId(), m_rfLC.getDstId());
    }

    // transmit group grant
    m_rfLC.setMessageType(RTCH_MESSAGE_TYPE_VCALL);
    writeRF_Message(false, true);

    m_rfLC.setMessageType(messageType);
    return true;
}

/// <summary>
/// Helper to write a deny packet.
/// </summary>
/// <param name="reason"></param>
/// <param name="service"></param>
void Trunk::writeRF_Message_Deny(uint8_t reason, uint8_t service)
{
    uint8_t messageType = m_rfLC.getMessageType();

    m_rfLC.setMessageType(service);
    m_rfLC.setCauseResponse(reason);

    if (m_verbose) {
        LogMessage(LOG_RF, "NXDN, MSG_DENIAL (Message Denial), reason = $%02X, service = $%02X, srcId = %u, dstId = %u", 
            service, m_rfLC.getSrcId(), m_rfLC.getDstId());
    }

    writeRF_Message(false);

    m_rfLC.setMessageType(messageType);
}

/// <summary>
/// Helper to write a group registration response packet.
/// </summary>
/// <param name="srcId"></param>
/// <param name="dstId"></param>
bool Trunk::writeRF_Message_Grp_Reg_Rsp(uint32_t srcId, uint32_t dstId)
{
    bool ret = false;

    m_rfLC.setMessageType(RCCH_MESSAGE_TYPE_GRP_REG);
    m_rfLC.setCauseResponse(NXDN_CAUSE_MM_REG_ACCEPTED);

    // validate the location ID
    if (m_rfLC.getLocId() != m_nxdn->m_siteData.locId()) {
        LogWarning(LOG_RF, "NXDN, " NXDN_RCCH_MSG_TYPE_GRP_REG_REQ " denial, LOCID rejection, locId = $%04X", m_rfLC.getLocId());
        ::ActivityLog("NXDN", true, "group affiliation request from %u denied", srcId);
        m_rfLC.setCauseResponse(NXDN_CAUSE_MM_REG_FAILED);
    }

    // validate the source RID
    if (!acl::AccessControl::validateSrcId(srcId)) {
        LogWarning(LOG_RF, "NXDN, " NXDN_RCCH_MSG_TYPE_GRP_REG_REQ " denial, RID rejection, srcId = %u", srcId);
        ::ActivityLog("NXDN", true, "group affiliation request from %u to %s %u denied", srcId, "TG ", dstId);
        m_rfLC.setCauseResponse(NXDN_CAUSE_MM_REG_FAILED);
    }

    // validate the source RID is registered
    if (!m_nxdn->m_affiliations.isUnitReg(srcId) && m_verifyReg) {
        LogWarning(LOG_RF, "NXDN, " NXDN_RCCH_MSG_TYPE_GRP_REG_REQ " denial, RID not registered, srcId = %u", srcId);
        ::ActivityLog("NXDN", true, "group affiliation request from %u to %s %u denied", srcId, "TG ", dstId);
        m_rfLC.setCauseResponse(NXDN_CAUSE_MM_REG_REFUSED);
    }

    // validate the talkgroup ID
    if (m_rfLC.getGroup()) {
        if (dstId == 0U) {
            LogWarning(LOG_RF, "NXDN, " NXDN_RCCH_MSG_TYPE_GRP_REG_REQ ", TGID 0, dstId = %u", dstId);
        }
        else {
            if (!acl::AccessControl::validateTGId(dstId)) {
                LogWarning(LOG_RF, "NXDN, " NXDN_RCCH_MSG_TYPE_GRP_REG_REQ " denial, TGID rejection, dstId = %u", dstId);
                ::ActivityLog("NXDN", true, "group affiliation request from %u to %s %u denied", srcId, "TG ", dstId);
                m_rfLC.setCauseResponse(NXDN_CAUSE_MM_LOC_ACPT_GRP_REFUSE);
            }
        }
    }

    if (m_rfLC.getCauseResponse() == NXDN_CAUSE_MM_REG_ACCEPTED) {
        if (m_verbose) {
            LogMessage(LOG_RF, "NXDN, " NXDN_RCCH_MSG_TYPE_GRP_REG_REQ ", srcId = %u, dstId = %u", srcId, dstId);
        }

        ::ActivityLog("NXDN", true, "group affiliation request from %u to %s %u", srcId, "TG ", dstId);
        ret = true;

        // update dynamic affiliation table
        m_nxdn->m_affiliations.groupAff(srcId, dstId);
    }

    writeRF_Message(false);
    return ret;
}

/// <summary>
/// Helper to write a unit registration response packet.
/// </summary>
/// <param name="srcId"></param>
void Trunk::writeRF_Message_U_Reg_Rsp(uint32_t srcId)
{
    m_rfLC.setMessageType(RCCH_MESSAGE_TYPE_REG);
    m_rfLC.setCauseResponse(NXDN_CAUSE_MM_REG_ACCEPTED);

    // validate the location ID
    if (m_rfLC.getLocId() != m_nxdn->m_siteData.locId()) {
        LogWarning(LOG_RF, "NXDN, " NXDN_RCCH_MSG_TYPE_REG_REQ " denial, LOCID rejection, locId = $%04X", m_rfLC.getLocId());
        ::ActivityLog("NXDN", true, "unit registration request from %u denied", srcId);
        m_rfLC.setCauseResponse(NXDN_CAUSE_MM_REG_FAILED);
    }

    // validate the source RID
    if (!acl::AccessControl::validateSrcId(srcId)) {
        LogWarning(LOG_RF, "NXDN, " NXDN_RCCH_MSG_TYPE_REG_REQ " denial, RID rejection, srcId = %u", srcId);
        ::ActivityLog("NXDN", true, "unit registration request from %u denied", srcId);
        m_rfLC.setCauseResponse(NXDN_CAUSE_MM_REG_FAILED);
    }

    if (m_rfLC.getCauseResponse() == NXDN_CAUSE_MM_REG_ACCEPTED) {
        if (m_verbose) {
            LogMessage(LOG_RF, "NXDN, " NXDN_RCCH_MSG_TYPE_REG_REQ ", srcId = %u, locId = %u", srcId, m_rfLC.getLocId());
        }

        ::ActivityLog("NXDN", true, "unit registration request from %u", srcId);

        // update dynamic unit registration table
        if (!m_nxdn->m_affiliations.isUnitReg(srcId)) {
            m_nxdn->m_affiliations.unitReg(srcId);
        }
    }

    m_rfLC.setSrcId(srcId);
    m_rfLC.setDstId(srcId);

    writeRF_Message(true);
}

/// <summary>
/// Helper to write a CC SITE_INFO broadcast packet on the RF interface.
/// </summary>
void Trunk::writeRF_CC_Message_Site_Info()
{
    if (m_debug) {
        LogMessage(LOG_RF, "NXDN, SITE_INFO (Site Information)");
    }

    uint8_t data[NXDN_FRAME_LENGTH_BYTES + 2U];
    ::memset(data + 2U, 0x00U, NXDN_FRAME_LENGTH_BYTES);

    Sync::addNXDNSync(data + 2U);

    // generate the LICH
    channel::LICH lich;
    lich.setRFCT(NXDN_LICH_RFCT_RCCH);
    lich.setFCT(NXDN_LICH_CAC_OUTBOUND);
    lich.setOption(NXDN_LICH_DATA_NORMAL);
    lich.setOutbound(true);
    lich.encode(data + 2U);

    uint8_t buffer[NXDN_RCCH_LC_LENGTH_BYTES];
    ::memset(buffer, 0x00U, NXDN_RCCH_LC_LENGTH_BYTES);

    m_rfLC.setMessageType(RCCH_MESSAGE_TYPE_SITE_INFO);
    m_rfLC.encode(buffer, NXDN_RCCH_LC_LENGTH_BITS);

    // generate the CAC
    channel::CAC cac;
    cac.setVerbose(m_dumpRCCH);
    cac.setRAN(m_nxdn->m_ran);
    cac.setStructure(NXDN_SR_RCCH_HEAD_SINGLE);
    cac.setData(buffer);
    cac.encode(data + 2U);

    data[0U] = modem::TAG_DATA;
    data[1U] = 0x00U;

    m_nxdn->scrambler(data + 2U);

    addPostBits(data + 2U);

    if (m_nxdn->m_duplex) {
        m_nxdn->addFrame(data, NXDN_FRAME_LENGTH_BYTES + 2U);
    }
}

/// <summary>
/// Helper to write a CC SRV_INFO broadcast packet on the RF interface.
/// </summary>
void Trunk::writeRF_CC_Message_Service_Info()
{
    if (m_debug) {
        LogMessage(LOG_RF, "NXDN, SRV_INFO (Service Information)");
    }

    uint8_t data[NXDN_FRAME_LENGTH_BYTES + 2U];
    ::memset(data + 2U, 0x00U, NXDN_FRAME_LENGTH_BYTES);

    Sync::addNXDNSync(data + 2U);

    // generate the LICH
    channel::LICH lich;
    lich.setRFCT(NXDN_LICH_RFCT_RCCH);
    lich.setFCT(NXDN_LICH_CAC_OUTBOUND);
    lich.setOption(NXDN_LICH_DATA_NORMAL);
    lich.setOutbound(true);
    lich.encode(data + 2U);

    uint8_t buffer[NXDN_RCCH_LC_LENGTH_BYTES];
    ::memset(buffer, 0x00U, NXDN_RCCH_LC_LENGTH_BYTES);

    m_rfLC.setMessageType(MESSAGE_TYPE_SRV_INFO);
    m_rfLC.encode(buffer, NXDN_RCCH_LC_LENGTH_BITS / 2U);
    m_rfLC.encode(buffer, NXDN_RCCH_LC_LENGTH_BITS / 2U, NXDN_RCCH_LC_LENGTH_BITS / 2U);

    // generate the CAC
    channel::CAC cac;
    cac.setVerbose(m_dumpRCCH);
    cac.setRAN(m_nxdn->m_ran);
    cac.setStructure(NXDN_SR_RCCH_DUAL);
    cac.setData(buffer);
    cac.encode(data + 2U);

    data[0U] = modem::TAG_DATA;
    data[1U] = 0x00U;

    m_nxdn->scrambler(data + 2U);

    addPostBits(data + 2U);

    if (m_nxdn->m_duplex) {
        m_nxdn->addFrame(data, NXDN_FRAME_LENGTH_BYTES + 2U);
    }
}

/// <summary>
/// Helper to add the post field bits on NXDN frame data.
/// </summary>
/// <param name="data"></param>
void Trunk::addPostBits(uint8_t* data)
{
    assert(data != NULL);

    // post field
    for (uint32_t i = 0U; i < NXDN_CAC_E_POST_FIELD_BITS; i++) {
        uint32_t n = i + NXDN_FSW_LENGTH_BITS + NXDN_LICH_LENGTH_BITS + NXDN_CAC_FEC_LENGTH_BITS + NXDN_CAC_E_POST_FIELD_BITS;
        bool b = READ_BIT(NXDN_PREAMBLE, i);
        WRITE_BIT(data, n, b);
    }
}