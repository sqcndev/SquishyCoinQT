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

#ifndef BITCOIN_CHAINPARAMS_H
#define BITCOIN_CHAINPARAMS_H

#include "chainparamsbase.h"
#include "consensus/params.h"
#include "primitives/block.h"
#include "protocol.h"

#define SQUISHY_MINDIFF_NBITS 0x200f0f0f

#include <vector>

/****
 * DNS seed info
 */
struct CDNSSeedData {
    std::string name, host;
    CDNSSeedData(const std::string &strName, const std::string &strHost) : name(strName), host(strHost) {}
};

/****
 * IPv6 seed info
 */
struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

typedef std::map<int, uint256> MapCheckpoints;


/**
 * CChainParams defines various tweakable parameters of a given instance of the
 * Bitcoin system. There are three: the main network on which people trade goods
 * and services, the public test network which gets reset from time to time and
 * a regression test mode which is intended for private networks only. It has
 * minimal difficulty to ensure that blocks can be found instantly.
 */
class CChainParams
{
public:
    enum Base58Type {
        PUBKEY_ADDRESS,
        SCRIPT_ADDRESS,
        SECRET_KEY,
        EXT_PUBLIC_KEY,
        EXT_SECRET_KEY,

        ZCPAYMENT_ADDRRESS,
        ZCSPENDING_KEY,
        ZCVIEWING_KEY,

        MAX_BASE58_TYPES
    };
    struct CCheckpointData {
        MapCheckpoints mapCheckpoints;
        int64_t nTimeLastCheckpoint;
        int64_t nTransactionsLastCheckpoint;
        double fTransactionsPerDay;
    };

    enum Bech32Type {
        SAPLING_PAYMENT_ADDRESS,
        SAPLING_FULL_VIEWING_KEY,
        SAPLING_INCOMING_VIEWING_KEY,
        SAPLING_EXTENDED_SPEND_KEY,

        MAX_BECH32_TYPES
    };

    /****
     * @returns parameters that influence chain consensus
     */
    const Consensus::Params& GetConsensus() const { return consensus; }
    /***
     * Message header start bytes
     * @returns 4 bytes
     */
    const CMessageHeader::MessageStartChars& MessageStart() const { return pchMessageStart; }
    /****
     * @returns bytes of public key that signs broadcast alert messages
     */
    const std::vector<unsigned char>& AlertKey() const { return vAlertPubKey; }
    /***
     * @returns default TCP port for P2P connections
     */
    int GetDefaultPort() const { return nDefaultPort; }

    /***
     * @returns the first block of the chain
     */
    const CBlock& GenesisBlock() const { return genesis; }
    /** 
     * Make miner wait to have peers to avoid wasting work
     * @returns true if peers are required before mining begins 
     */
    bool MiningRequiresPeers() const { return fMiningRequiresPeers; }
    /** 
     * Default value for -checkmempool and -checkblockindex argument
     * @returns true if mempool and indexes should be checked by default 
     */
    bool DefaultConsistencyChecks() const { return fDefaultConsistencyChecks; }
    /** 
     * Policy: Filter transactions that do not match well-defined patterns
     * @returns true to filter, false to be permissive 
     */
    bool RequireStandard() const { return fRequireStandard; }
    /****
     * @returns height where pruning should happen
     */
    int64_t PruneAfterHeight() const { return nPruneAfterHeight; }
    /****
     * @returns N value for equihash algo
     */
    unsigned int EquihashN() const { return nEquihashN; }
    /***
     * @returns K value for equihash algo
     */
    unsigned int EquihashK() const { return nEquihashK; }
    /****
     * @returns currency units (i.e. "KMD", "REG", "TAZ"
     */
    std::string CurrencyUnits() const { return strCurrencyUnits; }
    /****
     * @ref https://github.com/satoshilabs/slips/blob/master/slip-0044.md
     * @returns coin identifier for this chain
     */
    uint32_t BIP44CoinType() const { return bip44CoinType; }
    /** 
     * Make miner stop after a block is found. In RPC, don't return until nGenProcLimit blocks are generated
     * @returns true if on-demand block mining is allowed (true for RegTest, should probably be false for all others)
     */
    bool MineBlocksOnDemand() const { return fMineBlocksOnDemand; }
    /** 
     * Deprecated. Use NetworkIDString() to identify the network
     * @returns true if testnet
     */
    bool TestnetToBeDeprecatedFieldRPC() const { return fTestnetToBeDeprecatedFieldRPC; }
    /** 
     * Return the BIP70 network string ("main", "test" or "regtest") 
     * @returns the network ID
    */
    std::string NetworkIDString() const { return strNetworkID; }
    /****
     * @returns a vector of DNS entries to get seed data
     */
    const std::vector<CDNSSeedData>& DNSSeeds() const { return vSeeds; }
    /***
     * @param the type (i.e. PUBKEY_ADDRESS, SCRIPT_ADDRESS)
     * @returns prefix bytes to common encoded strings
     */
    const std::vector<unsigned char>& Base58Prefix(Base58Type type) const { return base58Prefixes[type]; }
    /****
     * @returns the Human Readable Part of a particular type of Bech32 data
     */
    const std::string& Bech32HRP(Bech32Type type) const { return bech32HRPs[type]; }
    /****
     * Use in case of problems with DNS
     * @returns hard-coded IPv6 addresses of seed nodes
     */
    const std::vector<SeedSpec6>& FixedSeeds() const { return vFixedSeeds; }
    const std::vector<std::pair<std::string, std::string> > GenesisNotaries() const { return genesisNotaries; }
    const CCheckpointData& Checkpoints() const { return checkpointData; }
    /** 
     * @returns the founder's reward address for a given block height 
     */
    std::string GetFoundersRewardAddressAtHeight(int height) const;
    /***
     * @returns the founder's reward script for a given block height
     */
    CScript GetFoundersRewardScriptAtHeight(int height) const;
    /***
     * @param i the index
     * @returns the founder's reward address
     */
    std::string GetFoundersRewardAddressAtIndex(int i) const;
    /** 
     * Enforce coinbase consensus rule in regtest mode 
     */
    void SetRegTestCoinbaseMustBeProtected() { consensus.fCoinbaseMustBeProtected = true; }

    /***
     * Set the default P2P IP port
     * @param port the new port
     */
    void SetDefaultPort(uint16_t port) { nDefaultPort = port; }
    /***
     * @param checkpointData the new data
     */
    void SetCheckpointData(CCheckpointData checkpointData);
    /***
     * @param n the new N value for equihash
     */
    void SetNValue(uint64_t n) { nEquihashN = n; }
    /****
     * @param k the new K value for equihash
     */
    void SetKValue(uint64_t k) { nEquihashK = k; }
    /****
     * @param flag true to require connected peers before mining can begin
     */
    void SetMiningRequiresPeers(bool flag) { fMiningRequiresPeers = flag; }
    uint32_t CoinbaseMaturity() const { return coinbaseMaturity; }
    void SetCoinbaseMaturity(uint32_t in) const { coinbaseMaturity = in; }

    CMessageHeader::MessageStartChars pchMessageStart; // message header start bytes
    Consensus::Params consensus; // parameters that influence consensus

protected:
    CChainParams() {}

    std::vector<unsigned char> vAlertPubKey; // Raw pub key bytes for the broadcast alert signing key
    int nMinerThreads = 0; // number of mining threads
    long nMaxTipAge = 0;
    int nDefaultPort = 0; // p2p
    uint64_t nPruneAfterHeight = 0;
    unsigned int nEquihashN = 0;
    unsigned int nEquihashK = 0;
    std::vector<CDNSSeedData> vSeeds;
    std::vector<unsigned char> base58Prefixes[MAX_BASE58_TYPES];
    std::string bech32HRPs[MAX_BECH32_TYPES];
    std::string strNetworkID;
    std::string strCurrencyUnits;
    uint32_t bip44CoinType;
    CBlock genesis;
    std::vector<SeedSpec6> vFixedSeeds;
    bool fMiningRequiresPeers = false;
    bool fDefaultConsistencyChecks = false;
    bool fRequireStandard = false;
    bool fMineBlocksOnDemand = false;
    bool fTestnetToBeDeprecatedFieldRPC = false;
    CCheckpointData checkpointData;
    std::vector<std::string> vFoundersRewardAddress;
    mutable uint32_t coinbaseMaturity = 100; // allow to modify by -ac_cbmaturity
    std::vector< std::pair<std::string, std::string> > genesisNotaries;
};

/**
 * NOTE: This won't change after app startup (except for unit tests)
 * @returns the currently selected parameters for this chain
 */
const CChainParams &Params();

/** 
 * @param network the network
 * @returns parameters for the given network. 
 */
CChainParams &Params(CBaseChainParams::Network network);

/** 
 * Sets the params returned by Params() to those for the given network.
 * @param network the network to use
 */
void SelectParams(CBaseChainParams::Network network);

/**
 * Looks for -regtest or -testnet and then calls SelectParams as appropriate.
 * @returns false if an invalid combination is given.
 */
bool SelectParamsFromCommandLine();

/**
 * Allows modifying the network upgrade regtest parameters.
 * @param idx the index of the new parameters
 * @param nActivationHeight when to activate
 */
void UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex idx, int nActivationHeight);

void squishy_setactivation(int32_t height);
int32_t MAX_BLOCK_SIZE(int32_t height);

#endif // BITCOIN_CHAINPARAMS_H
