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

#include "cc/eval.h"
#include "crosschain.h"
#include "importcoin.h"
#include "main.h"
#include "merkleblock.h"
#include "hex.h"
#include "squishy_bitcoind.h"
#include "cc/CCinclude.h"
#include "squishy_notary.h"
#include "notarisationdb.h"
#include "cc/import.h"

/*
 * The crosschain workflow.
 *
 * 3 chains, A, B, and KMD. We would like to prove TX on B.
 * There is a notarisation, nA0, which will include TX via an MoM.
 * The notarisation nA0 must fall between 2 notarisations of B,
 * ie, nB0 and nB1. An MoMoM including this range is propagated to
 * B in notarisation receipt (backnotarisation) bnB2.
 *
 * A:                 TX   bnA0
 *                     \   /
 * KMD:      nB0        nA0     nB1      nB2
 *              \                 \       \
 * B:          bnB0              bnB1     bnB2
 */

// XXX: There are potential crashes wherever we access chainActive without a lock,
// because it might be disconnecting blocks at the same time.


int NOTARISATION_SCAN_LIMIT_BLOCKS = 1440;

/****
 * Determine the type of crosschain
 * @param symbol the asset chain to check
 * @returns the type of chain
 */
CrosschainType CrossChain::GetSymbolAuthority(const std::string& symbol)
{
    if (symbol.find("TXSCL") == 0)
        return CROSSCHAIN_TXSCL;

    if (is_STAKED(symbol.c_str()) != 0)
        return CROSSCHAIN_STAKED;

    return CROSSCHAIN_SQUISHY;
}

/***
 * @param tx the transaction to check
 * @param auth the authority object
 * @returns true on success
 */
bool CrossChain::CheckTxAuthority(const CTransaction &tx, CrosschainAuthority auth)
{
    if (tx.vin.size() < auth.requiredSigs) 
        return false;

    uint8_t seen[64] = {0};
    for(const CTxIn &txIn : tx.vin) // check each vIn
    {
        // Get notary pubkey
        CTransaction tx;
        uint256 hashBlock;
        EvalRef eval;
        if (!eval->GetTxUnconfirmed(txIn.prevout.hash, tx, hashBlock)) 
            return false;
        if (tx.vout.size() < txIn.prevout.n) 
            return false;
        CScript spk = tx.vout[txIn.prevout.n].scriptPubKey;
        if (spk.size() != 35) 
            return false;
        const unsigned char *pk = &spk[0];
        if (pk++[0] != 33) 
            return false;
        if (pk[33] != OP_CHECKSIG) 
            return false;

        // Check it's a notary
        for (int i=0; i<auth.size; i++) {
            if (!seen[i]) {
                if (memcmp(pk, auth.notaries[i], 33) == 0) {
                    seen[i] = 1;
                    goto found;
                }
            }
        }

        return false;
        found:;
    }
    return true;
}

/*****
 * @brief Calculate the proof root
 * @note this happens on the KMD chain
 * @param symbol the chain symbol
 * @param targetCCid
 * @param kmdHeight
 * @param moms collection of MoMs
 * @param destNotarisationTxid
 * @returns the proof root, or 0 on error
 */
uint256 CrossChain::CalculateProofRoot(const char* symbol, uint32_t targetCCid, int kmdHeight,
        std::vector<uint256> &moms, uint256 &destNotarisationTxid)
{
    /*
     * Notaries don't wait for confirmation on KMD before performing a backnotarisation,
     * but we need a determinable range that will encompass all merkle roots. Include MoMs
     * including the block height of the last notarisation until the height before the
     * previous notarisation.
     *
     *    kmdHeight      notarisations-0      notarisations-1
     *                         *********************|
     *        > scan backwards >
     */

    if (targetCCid < 2)
        return uint256();

    if (kmdHeight < 0 || kmdHeight > chainActive.Height())
        return uint256();

    int seenOwnNotarisations = 0;
    CrosschainType authority = GetSymbolAuthority(symbol);
    std::set<uint256> tmp_moms;

    for (int i=0; i<NOTARISATION_SCAN_LIMIT_BLOCKS; i++) {
        if (i > kmdHeight) break;
        NotarisationsInBlock notarisations;
        uint256 blockHash = *chainActive[kmdHeight-i]->phashBlock;
        if (!GetBlockNotarisations(blockHash, notarisations))
            continue;

        // See if we have an own notarisation in this block
        for(Notarisation& nota : notarisations) {
            if (strcmp(nota.second.symbol, symbol) == 0)
            {
                seenOwnNotarisations++;
                if (seenOwnNotarisations == 1)
                    destNotarisationTxid = nota.first;
                else if (seenOwnNotarisations == 7)
                    goto end;
                //break;
            }
        }

        if (seenOwnNotarisations >= 1) {
            for(Notarisation& nota : notarisations) {
                if (GetSymbolAuthority(nota.second.symbol) == authority)
                    if (nota.second.ccId == targetCCid) {
                      tmp_moms.insert(nota.second.MoM);
                      //LogPrintf( "added mom: %s\n",nota.second.MoM.GetHex().data());
                    }
            }
        }
    }

    // Not enough own notarisations found to return determinate MoMoM
    destNotarisationTxid = uint256();
    moms.clear();
    return uint256();

end:
    // add set to vector. Set makes sure there are no dupes included. 
    moms.clear();
    std::copy(tmp_moms.begin(), tmp_moms.end(), std::back_inserter(moms));
    return GetMerkleRoot(moms);
}


/*****
 * @brief Get a notarisation from a given height
 * @note Will scan notarisations leveldb up to a limit
 * @param[in] nHeight the height
 * @param[in] f
 * @param[out] found
 * @returns the height of the notarisation
 */
template <typename IsTarget>
int ScanNotarisationsFromHeight(int nHeight, const IsTarget f, Notarisation &found)
{
    int limit = std::min(nHeight + NOTARISATION_SCAN_LIMIT_BLOCKS, chainActive.Height());
    int start = std::max(nHeight, 1);

    for (int h=start; h<limit; h++) {
        NotarisationsInBlock notarisations;

        if (!GetBlockNotarisations(*chainActive[h]->phashBlock, notarisations))
            continue;

        for(auto entry : notarisations) {
            if (f(entry)) {
                found = entry;
                return h;
            }
        }
    }
    return 0;
}

/******
 * @brief
 * @note this happens on the KMD chain
 * @param txid
 * @param targetSymbol
 * @param targetCCid
 * @param assetChainProof
 * @param offset
 * @returns a pair of target chain notarisation txid and the merkle branch
 */
TxProof CrossChain::GetCrossChainProof(const uint256 txid, const char* targetSymbol, uint32_t targetCCid,
        const TxProof assetChainProof, int32_t offset)
{
    /*
     * Here we are given a proof generated by an assetchain A which goes from given txid to
     * an assetchain MoM. We need to go from the notarisationTxid for A to the MoMoM range of the
     * backnotarisation for B (given by kmdheight of notarisation), find the MoM within the MoMs for
     * that range, and finally extend the proof to lead to the MoMoM (proof root).
     */
    EvalRef eval;
    uint256 MoM = assetChainProof.second.Exec(txid);

    // Get a kmd height for given notarisation Txid
    int kmdHeight;
    {
        CTransaction sourceNotarisation;
        uint256 hashBlock;
        CBlockIndex blockIdx;
        if (!eval->GetTxConfirmed(assetChainProof.first, sourceNotarisation, blockIdx))
            throw std::runtime_error("Notarisation not found");
        kmdHeight = blockIdx.nHeight;
    }

    // We now have a kmdHeight of the notarisation from chain A. So we know that a MoM exists
    // at that height.
    // If we call CalculateProofRoot with that height, it'll scan backwards, until it finds
    // a notarisation from B, and it might not include our notarisation from A
    // at all. So, the thing we need to do is scan forwards to find the notarisation for B,
    // that is inclusive of A.
    Notarisation nota;
    auto isTarget = [&](Notarisation &nota) {
        return strcmp(nota.second.symbol, targetSymbol) == 0;
    };
    kmdHeight = ScanNotarisationsFromHeight(kmdHeight, isTarget, nota);
    if (!kmdHeight)
        throw std::runtime_error("Cannot find notarisation for target inclusive of source");
        
    if ( offset != 0 )
        kmdHeight += offset;

    // Get MoMs for kmd height and symbol
    std::vector<uint256> moms;
    uint256 targetChainNotarisationTxid;
    uint256 MoMoM = CalculateProofRoot(targetSymbol, targetCCid, kmdHeight, moms, targetChainNotarisationTxid);
    if (MoMoM.IsNull())
        throw std::runtime_error("No MoMs found");

    // Find index of source MoM in MoMoM
    int nIndex;
    for (nIndex=0; nIndex<moms.size(); nIndex++) {
        if (moms[nIndex] == MoM)
            goto cont;
    }
    throw std::runtime_error("Couldn't find MoM within MoMoM set");
cont:

    // Create a branch
    std::vector<uint256> vBranch;
    {
        CBlock fakeBlock;
        for (int i=0; i<moms.size(); i++) {
            CTransaction fakeTx;
            // first value in CTransaction memory is it's hash
            memcpy((void*)&fakeTx, moms[i].begin(), 32);
            fakeBlock.vtx.push_back(fakeTx);
        }
        vBranch = fakeBlock.GetMerkleBranch(nIndex);
    }

    // Concatenate branches
    MerkleBranch newBranch = assetChainProof.second;
    newBranch << MerkleBranch(nIndex, vBranch);

    // Check proof
    if (newBranch.Exec(txid) != MoMoM)
        throw std::runtime_error("Proof check failed");

    return std::make_pair(targetChainNotarisationTxid,newBranch);
}


/*****
 * @brief Takes an importTx that has proof leading to assetchain root and extends proof to cross chain root
 * @param importTx
 * @param offset
 */
void CrossChain::CompleteImportTransaction(CTransaction &importTx, int32_t offset)
{
    ImportProof proof; 
    CTransaction burnTx; 
    std::vector<CTxOut> payouts; 

    if (!UnmarshalImportTx(importTx, proof, burnTx, payouts))
        throw std::runtime_error("Couldn't unmarshal importTx");

    std::string targetSymbol;
    uint32_t targetCCid;
    uint256 payoutsHash;
    std::vector<uint8_t> rawproof;
    if (!UnmarshalBurnTx(burnTx, targetSymbol, &targetCCid, payoutsHash, rawproof))
        throw std::runtime_error("Couldn't unmarshal burnTx");

    TxProof merkleBranch;
    if( !proof.IsMerkleBranch(merkleBranch) )
        throw std::runtime_error("Incorrect import tx proof");
    TxProof newMerkleBranch = GetCrossChainProof(burnTx.GetHash(), targetSymbol.data(), 
            targetCCid, merkleBranch, offset);
    ImportProof newProof(newMerkleBranch);

    importTx = MakeImportCoinTransaction(newProof, burnTx, payouts);
}

/*****
 * @param nota the notarisation
 * @returns true if the notarization belongs to this chain
 */
bool IsSameAssetChain(const Notarisation &nota) {
    return chainName.isSymbol(nota.second.symbol);
};

/****
 * @brief Check MoMoM
 * @note on Assetchain
 * @param kmdNotarisationHash the hash
 * @param momom what to check
 * @returns true on success
 */
bool CrossChain::CheckMoMoM(uint256 kmdNotarisationHash, uint256 momom)
{
    /*
     * Given a notarisation hash and an MoMoM. Backnotarisations may arrive out of order
     * or multiple in the same block. So dereference the notarisation hash to the corresponding
     * backnotarisation and scan around the kmdheight to see if the MoMoM is a match.
     * This is a sledgehammer approach...
     */

    Notarisation bn;
    if (!GetBackNotarisation(kmdNotarisationHash, bn))
        return false;

    // Need to get block height of that backnotarisation
    EvalRef eval;
    CBlockIndex block;
    CTransaction tx;
    if (!eval->GetTxConfirmed(bn.first, tx, block)){
        LogPrintf( "Can't get height of backnotarisation, this should not happen\n");
        return false;
    }

    Notarisation nota;
    auto checkMoMoM = [&](Notarisation &nota) {
        return nota.second.MoMoM == momom;
    };

    return (bool) ScanNotarisationsFromHeight(block.nHeight-100, checkMoMoM, nota);

}

/*****
* @brief Check notaries approvals for the txoutproofs of burn tx
* @note alternate check if MoMoM check has failed
* @param burntxid - txid of burn tx on the source chain
* @param notaryTxids txids of notaries' proofs
* @returns true on success
*/
bool CrossChain::CheckNotariesApproval(uint256 burntxid, const std::vector<uint256> & notaryTxids) 
{
    int count = 0;

    // get notaries:
    uint8_t notaries_pubkeys[64][33];
    std::vector< std::vector<uint8_t> > alreadySigned;

    //unmarshal notaries approval txids
    for(auto notarytxid : notaryTxids ) {
        EvalRef eval;
        CBlockIndex block;
        CTransaction notarytx;  // tx with notary approval of txproof existence

        // get notary approval tx
        if (eval->GetTxConfirmed(notarytxid, notarytx, block)) {
            
            std::vector<uint8_t> vopret;
            if (!notarytx.vout.empty() && GetOpReturnData(notarytx.vout.back().scriptPubKey, vopret)) {
                std::vector<uint8_t> txoutproof;

                if (E_UNMARSHAL(vopret, ss >> txoutproof)) {
                    CMerkleBlock merkleBlock;
                    std::vector<uint256> prooftxids;
                    // extract block's merkle tree
                    if (E_UNMARSHAL(txoutproof, ss >> merkleBlock)) {

                        // extract proven txids:
                        merkleBlock.txn.ExtractMatches(prooftxids);
                        if (merkleBlock.txn.ExtractMatches(prooftxids) != merkleBlock.header.hashMerkleRoot ||  // check block merkle root is correct
                            std::find(prooftxids.begin(), prooftxids.end(), burntxid) != prooftxids.end()) {    // check burn txid is in proven txids list
                            
                            if (squishy_notaries(notaries_pubkeys, block.nHeight, block.GetBlockTime()) >= 0) {
                                // check it is a notary who signed approved tx:
                                int i;
                                for (i = 0; i < sizeof(notaries_pubkeys) / sizeof(notaries_pubkeys[0]); i++) {
                                    std::vector<uint8_t> vnotarypubkey(notaries_pubkeys[i], notaries_pubkeys[i] + 33);
#ifdef TESTMODE
                                    char test_notary_pubkey_hex[] = "029fa302968bbae81f41983d2ec20445557b889d31227caec5d910d19b7510ef86";
                                    uint8_t test_notary_pubkey33[33];
                                    decode_hex(test_notary_pubkey33, 33, test_notary_pubkey_hex);
#endif
                                    if (CheckVinPubKey(notarytx, 0, notaries_pubkeys[i])   // is signed by a notary?
                                        && std::find(alreadySigned.begin(), alreadySigned.end(), vnotarypubkey) == alreadySigned.end()   // check if notary not re-used
#ifdef TESTMODE                                        
                                        || CheckVinPubKey(notarytx, 0, test_notary_pubkey33)  // test
#endif
                                    )   
                                    {
                                        alreadySigned.push_back(vnotarypubkey);
                                        count++;
                                        LOGSTREAM("importcoin", CCLOG_DEBUG1, stream << "CheckNotariesApproval() notary approval checked, count=" << count << std::endl);
                                        break;
                                    }
                                }
                                if (i == sizeof(notaries_pubkeys) / sizeof(notaries_pubkeys[0]))
                                    LOGSTREAM("importcoin", CCLOG_DEBUG1, stream << "CheckNotariesApproval() txproof not signed by a notary or reused" << std::endl);
                            }
                            else {
                                LOGSTREAM("importcoin", CCLOG_INFO, stream << "CheckNotariesApproval() cannot get current notaries pubkeys" << std::endl);
                            }
                        }
                        else  {
                            LOGSTREAM("importcoin", CCLOG_INFO, stream << "CheckNotariesApproval() burntxid not found in txoutproof or incorrect txoutproof" << std::endl);
                        }
                    }
                    else {
                        LOGSTREAM("importcoin", CCLOG_INFO, stream << "CheckNotariesApproval() could not unmarshal merkleBlock" << std::endl);
                    }
                }
                else {
                    LOGSTREAM("importcoin", CCLOG_INFO, stream << "CheckNotariesApproval() could not unmarshal txoutproof" << std::endl);
                }
            }
            else {
                LOGSTREAM("importcoin", CCLOG_INFO, stream << "CheckNotariesApproval() no opret in the notary tx" << std::endl);
            }
        }
        else {
            LOGSTREAM("importcoin", CCLOG_INFO, stream << "CheckNotariesApproval() could not load notary tx" << std::endl);
        }
    }

    bool retcode;
#ifdef TESTMODE
    if (count < 1) { // 1 for test
#else
    if (count < 5) { 
#endif
        LOGSTREAM("importcoin", CCLOG_INFO, stream << "CheckNotariesApproval() not enough signed notary transactions=" << count << std::endl);
        retcode = false;
    }
    else
        retcode = true;

    return retcode;
}


/*****
 * @brief get the proof
 * @note On assetchain
 * @param hash
 * @param burnTx
 * @returns a pair containing the notarisation tx hash and the merkle branch
 */
TxProof CrossChain::GetAssetchainProof(uint256 hash,CTransaction burnTx)
{
    int nIndex;
    CBlockIndex* blockIndex;
    Notarisation nota;
    std::vector<uint256> branch;

    {
        uint256 blockHash;
        CTransaction tx;
        if (!GetTransaction(hash, tx, blockHash, true))
            throw std::runtime_error("cannot find transaction");

        if (blockHash.IsNull())
            throw std::runtime_error("tx still in mempool");

        blockIndex = squishy_getblockindex(blockHash);
        int h = blockIndex->nHeight;
        // The assumption here is that the first notarisation for a height GTE than
        // the transaction block height will contain the corresponding MoM. If there
        // are sequence issues with the notarisations this may fail.
        auto isTarget = [&](Notarisation &nota) {
            if (!IsSameAssetChain(nota)) return false;
            return nota.second.height >= blockIndex->nHeight;
        };
        if (!ScanNotarisationsFromHeight(blockIndex->nHeight, isTarget, nota))
            throw std::runtime_error("backnotarisation not yet confirmed");

        // index of block in MoM leaves
        nIndex = nota.second.height - blockIndex->nHeight;
    }

    // build merkle chain from blocks to MoM
    {
        std::vector<uint256> leaves, tree;
        for (int i=0; i<nota.second.MoMDepth; i++) {
            uint256 mRoot = chainActive[nota.second.height - i]->hashMerkleRoot;
            leaves.push_back(mRoot);
        }
        bool fMutated;
        BuildMerkleTree(&fMutated, leaves, tree);
        branch = GetMerkleBranch(nIndex, leaves.size(), tree);

        // Check branch
        uint256 ourResult = SafeCheckMerkleBranch(blockIndex->hashMerkleRoot, branch, nIndex);
        if (nota.second.MoM != ourResult)
            throw std::runtime_error("Failed merkle block->MoM");
    }

    // Now get the tx merkle branch
    {
        CBlock block;

        if (fHavePruned && !(blockIndex->nStatus & BLOCK_HAVE_DATA) && blockIndex->nTx > 0)
            throw std::runtime_error("Block not available (pruned data)");

        if(!ReadBlockFromDisk(block, blockIndex,1))
            throw std::runtime_error("Can't read block from disk");

        // Locate the transaction in the block
        int nTxIndex;
        for (nTxIndex = 0; nTxIndex < (int)block.vtx.size(); nTxIndex++)
            if (block.vtx[nTxIndex].GetHash() == hash)
                break;

        if (nTxIndex == (int)block.vtx.size())
            throw std::runtime_error("Error locating tx in block");

        std::vector<uint256> txBranch = block.GetMerkleBranch(nTxIndex);

        // Check branch
        if (block.hashMerkleRoot != CBlock::CheckMerkleBranch(hash, txBranch, nTxIndex))
            throw std::runtime_error("Failed merkle tx->block");

        // concatenate branches
        nIndex = (nIndex << txBranch.size()) + nTxIndex;
        branch.insert(branch.begin(), txBranch.begin(), txBranch.end());
    }

    // Check the proof
    if (nota.second.MoM != CBlock::CheckMerkleBranch(hash, branch, nIndex))
        throw std::runtime_error("Failed validating MoM");

    // All done!
    CDataStream ssProof(SER_NETWORK, PROTOCOL_VERSION);
    return std::make_pair(nota.second.txHash, MerkleBranch(nIndex, branch));
}
