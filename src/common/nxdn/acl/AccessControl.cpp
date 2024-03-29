// SPDX-License-Identifier: GPL-2.0-only
/**
* Digital Voice Modem - Common Library
* GPLv2 Open Source. Use is subject to license terms.
* DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
*
* @package DVM / Common Library
* @derivedfrom MMDVMHost (https://github.com/g4klx/MMDVMHost)
* @license GPLv2 License (https://opensource.org/licenses/GPL-2.0)
*
*   Copyright (C) 2016 Simon Rune, G7RZU
*   Copyright (C) 2016,2017 Jonathan Naylor, G4KLX
*   Copyright (C) 2017,2019,2024 Bryan Biedenkapp, N2PLL
*
*/
#include "Defines.h"
#include "nxdn/acl/AccessControl.h"

using namespace nxdn::acl;

// ---------------------------------------------------------------------------
//  Static Class Members
// ---------------------------------------------------------------------------

RadioIdLookup* AccessControl::m_ridLookup;
TalkgroupRulesLookup* AccessControl::m_tidLookup;

/// <summary>
/// Initializes the NXDN access control.
/// </summary>
/// <param name="ridLookup">Instance of the RadioIdLookup class.</param>
/// <param name="tidLookup">Instance of the TalkgroupRulesLookup class.</param>
void AccessControl::init(RadioIdLookup* ridLookup, TalkgroupRulesLookup* tidLookup)
{
    m_ridLookup = ridLookup;
    m_tidLookup = tidLookup;
}

/// <summary>
/// Helper to validate a source radio ID.
/// </summary>
/// <param name="id">Source Radio ID.</param>
/// <returns>True, if source radio ID is valid, otherwise false.</returns>
bool AccessControl::validateSrcId(uint32_t id)
{
    // check if RID ACLs are enabled
    if (!m_ridLookup->getACL()) {
        RadioId rid = m_ridLookup->find(id);
        if (!rid.radioDefault() && !rid.radioEnabled()) {
            return false;
        }

        return true;
    }

    // lookup RID and perform test for validity
    RadioId rid = m_ridLookup->find(id);
    if (!rid.radioEnabled())
        return false;

    return true;
}

/// <summary>
/// Helper to validate a talkgroup ID.
/// </summary>
/// <param name="id">Talkgroup ID.</param>
/// <returns>True, if talkgroup ID is valid, otherwise false.</returns>
bool AccessControl::validateTGId(uint32_t id)
{
    // TG0 is never valid
    if (id == 0U)
        return false;

    // check if TID ACLs are enabled
    if (!m_tidLookup->getACL()) {
        return true;
    }

    // lookup TID and perform test for validity
    TalkgroupRuleGroupVoice tid = m_tidLookup->find(id);
    if (tid.isInvalid())
        return false;

    if (!tid.config().active())
        return false;

    return true;
}

/// <summary>
/// Helper to determine if a talkgroup ID is non-preferred.
/// </summary>
/// <param name="id">Talkgroup ID.</param>
/// <returns>True, if talkgroup ID is valid, otherwise false.</returns>
bool AccessControl::tgidNonPreferred(uint32_t id)
{
    // TG0 is never valid
    if (id == 0U)
        return false;

    // check if TID ACLs are enabled
    if (!m_tidLookup->getACL()) {
        return false;
    }

    // lookup TID and perform test for validity
    TalkgroupRuleGroupVoice tid = m_tidLookup->find(id);
    if (tid.config().nonPreferred())
        return true;

    return false;
}
