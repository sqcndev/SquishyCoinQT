// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#include "chain.h"
#include "squishy_defs.h"
#include "squishy_globals.h"
#include "notaries_staked.h"
#include "squishy_hardfork.h"

using namespace std;

#include "main.h"
#include "txdb.h"

/**
 * CChain implementation
 */
void CChain::SetTip(CBlockIndex *pindex) {
    AssertLockHeld(cs_main);
    if (pindex == NULL) {
        vChain.clear();
        return;
    }
    vChain.resize(pindex->nHeight + 1);
    while (pindex && vChain[pindex->nHeight] != pindex) {
        vChain[pindex->nHeight] = pindex;
        pindex = pindex->pprev;
    }
}

CBlockLocator CChain::GetLocator(const CBlockIndex *pindex) const {
    AssertLockHeld(cs_main);
    int nStep = 1;
    std::vector<uint256> vHave;
    vHave.reserve(32);

    if (!pindex)
        pindex = Tip();
    while (pindex) {
        vHave.push_back(pindex->GetBlockHash());
        // Stop when we have added the genesis block.
        if (pindex->nHeight == 0)
            break;
        // Exponentially larger steps back, plus the genesis block.
        int nHeight = std::max(pindex->nHeight - nStep, 0);
        if (Contains(pindex)) {
            // Use O(1) CChain index if possible.
            pindex = (*this)[nHeight];
        } else {
            // Otherwise, use O(log n) skiplist.
            pindex = pindex->GetAncestor(nHeight);
        }
        if (vHave.size() > 10)
            nStep *= 2;
    }

    return CBlockLocator(vHave);
}

const CBlockIndex *CChain::FindFork(const CBlockIndex *pindex) const {
    AssertLockHeld(cs_main);
    if ( pindex == 0 )
        return(0);
    if (pindex->nHeight > Height())
        pindex = pindex->GetAncestor(Height());
    while (pindex && !Contains(pindex))
        pindex = pindex->pprev;
    return pindex;
}

void CBlockIndex::TrimSolution()
{
    AssertLockHeld(cs_main);

    // We can correctly trim a solution as soon as the block index entry has been added
    // to leveldb. Updates to the block index entry (to update validity status) will be
    // handled by re-reading the solution from the existing db entry. It does not help to
    // try to avoid these reads by gating trimming on the validity status: the re-reads are
    // efficient anyway because of caching in leveldb, and most of them are unavoidable.
    if (HasSolution()) {
        std::vector<unsigned char> empty;
        nSolution.swap(empty);
    }
}

CBlockHeader CBlockIndex::GetBlockHeader() const
{
    AssertLockHeld(cs_main);

    CBlockHeader header;
    header.nVersion             = nVersion;
    if (pprev) {
        header.hashPrevBlock    = pprev->GetBlockHash();
    }
    header.hashMerkleRoot       = hashMerkleRoot;
    header.hashFinalSaplingRoot = hashFinalSaplingRoot;
    header.nTime                = nTime;
    header.nBits                = nBits;
    header.nNonce               = nNonce;
    if (HasSolution()) {
        header.nSolution        = nSolution;
    } else {
        CDiskBlockIndex dbindex;
        if (!pblocktree->ReadDiskBlockIndex(GetBlockHash(), dbindex)) {
            LogPrintf("%s: Failed to read index entry", __func__);
            throw std::runtime_error("Failed to read index entry");
        }
        header.nSolution        = dbindex.GetSolution();
    }
    return header;
}

/** Turn the lowest '1' bit in the binary representation of a number into a '0'. */
int static inline InvertLowestOne(int n) { return n & (n - 1); }

/** Compute what height to jump back to with the CBlockIndex::pskip pointer. */
int static inline GetSkipHeight(int height) {
    if (height < 2)
        return 0;

    // Determine which height to jump back to. Any number strictly lower than height is acceptable,
    // but the following expression seems to perform well in simulations (max 110 steps to go back
    // up to 2**18 blocks).
    return (height & 1) ? InvertLowestOne(InvertLowestOne(height - 1)) + 1 : InvertLowestOne(height);
}

CBlockIndex* CBlockIndex::GetAncestor(int height)
{
    if (height > nHeight || height < 0)
        return NULL;

    CBlockIndex* pindexWalk = this;
    int heightWalk = nHeight;
    while ( heightWalk > height && pindexWalk != 0 )
    {
        int heightSkip = GetSkipHeight(heightWalk);
        int heightSkipPrev = GetSkipHeight(heightWalk - 1);
        if (pindexWalk->pskip != NULL &&
            (heightSkip == height ||
             (heightSkip > height && !(heightSkipPrev < heightSkip - 2 &&
                                       heightSkipPrev >= height)))) {
            // Only follow pskip if pprev->pskip isn't better than pskip->pprev.
            pindexWalk = pindexWalk->pskip;
            heightWalk = heightSkip;
        } else {
            assert(pindexWalk->pprev);
            pindexWalk = pindexWalk->pprev;
            heightWalk--;
        }
    }
    return pindexWalk;
}

const CBlockIndex* CBlockIndex::GetAncestor(int height) const
{
    return const_cast<CBlockIndex*>(this)->GetAncestor(height);
}

void CBlockIndex::BuildSkip()
{
    if (pprev)
        pskip = pprev->GetAncestor(GetSkipHeight(nHeight));
}

bool CDiskBlockIndex::isStakedAndNotaryPay() const
{
    return is_STAKED(chainName.symbol()) != 0 && ASSETCHAINS_NOTARY_PAY[0] != 0;
}

bool CDiskBlockIndex::isStakedAndAfterDec2019(unsigned int nTime) const
{
    return ASSETCHAINS_STAKED != 0 && (nTime > nStakedDecemberHardforkTimestamp || is_STAKED(chainName.symbol()) != 0);
}
