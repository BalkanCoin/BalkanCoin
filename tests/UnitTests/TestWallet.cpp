// Copyright (c) 2012-2015, The CryptoNote developers, The Bytecoin developers
//
// This file is part of Bytecoin.
//
// Bytecoin is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Bytecoin is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Bytecoin.  If not, see <http://www.gnu.org/licenses/>.

#include "gtest/gtest.h"

#include <system_error>
#include <chrono>
#include <numeric>

#include "Common/StringTools.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteCore/TransactionApi.h"
#include "CryptoNoteCore/TransactionApiExtra.h"
#include "INodeStubs.h"
#include "TestBlockchainGenerator.h"
#include "TransactionApiHelpers.h"
#include <Logging/ConsoleLogger.h>
#include "Wallet/WalletGreen.h"
#include "WalletLegacy/WalletUserTransactionsCache.h"
#include "WalletLegacy/WalletLegacySerializer.h"
#include <System/Dispatcher.h>
#include <System/Timer.h>

using namespace Crypto;
using namespace Common;
using namespace CryptoNote;

namespace CryptoNote {
    bool operator==(const WalletTransaction& lhs, const WalletTransaction& rhs) {
      if (lhs.state != rhs.state) {
        return false;
      }

      if (lhs.timestamp != rhs.timestamp) {
        return false;
      }

      if (lhs.blockHeight != rhs.blockHeight) {
        return false;
      }

      if (lhs.hash != rhs.hash) {
        return false;
      }

      if (lhs.totalAmount != rhs.totalAmount) {
        return false;
      }

      if (lhs.fee != rhs.fee) {
        return false;
      }

      if (lhs.creationTime != rhs.creationTime) {
        return false;
      }

      if (lhs.unlockTime != rhs.unlockTime) {
        return false;
      }

      if (lhs.extra != rhs.extra) {
        return false;
      }

      return true;
    }

    bool operator!=(const WalletTransaction& lhs, const WalletTransaction& rhs) {
      return !(lhs == rhs);
    }

    bool operator==(const WalletTransfer& lhs, const WalletTransfer& rhs) {
      if (lhs.address != rhs.address) {
        return false;
      }

      if (lhs.amount != rhs.amount) {
        return false;
      }

      return true;
    }

    bool operator!=(const WalletTransfer& lhs, const WalletTransfer& rhs) {
      return !(lhs == rhs);
    }
}

class WalletApi: public ::testing::Test {
public:
  WalletApi() :
    currency(CryptoNote::CurrencyBuilder(logger).currency()),
    generator(currency),
    node(generator),
    alice(dispatcher, currency, node),
    FEE(currency.minimumFee())
  { }

  virtual void SetUp() override;
  virtual void TearDown() override;

protected:
  CryptoNote::AccountPublicAddress parseAddress(const std::string& address);
  void generateBlockReward();
  void generateBlockReward(const std::string& address);
  void generateAndUnlockMoney();
  void generateAddressesWithPendingMoney(size_t count);
  void unlockMoney();
  void unlockMoney(CryptoNote::WalletGreen& wallet, INodeTrivialRefreshStub& inode);

  template<typename T>
  void waitValueChanged(CryptoNote::WalletGreen& wallet, T prev, std::function<T ()>&& f);

  template<typename T>
  void waitForValue(CryptoNote::WalletGreen& wallet, T value, std::function<T ()>&& f);

  void waitActualBalanceUpdated();
  void waitActualBalanceUpdated(uint64_t prev);
  void waitActualBalanceUpdated(CryptoNote::WalletGreen& wallet, uint64_t prev);

  void waitPendingBalanceUpdated();
  void waitPendingBalanceUpdated(uint64_t prev);
  void waitPendingBalanceUpdated(CryptoNote::WalletGreen& wallet, uint64_t prev);

  void waitForTransactionCount(CryptoNote::WalletGreen& wallet, uint64_t expected);
  void waitForActualBalance(uint64_t expected);

  size_t sendMoneyToRandomAddressFrom(const std::string& address, uint64_t amount, uint64_t fee);
  size_t sendMoneyToRandomAddressFrom(const std::string& address);

  size_t sendMoney(const std::string& to, int64_t amount, uint64_t fee, uint64_t mixIn = 0, const std::string& extra = "", uint64_t unlockTimestamp = 0);

  void fillWalletWithDetailsCache();

  void wait(uint64_t milliseconds);
  void testIWalletDataCompatibility(bool details, const std::string& cache = std::string(),
          const std::vector<WalletLegacyTransaction>& txs = std::vector<WalletLegacyTransaction>(),
          const std::vector<WalletLegacyTransfer>& trs = std::vector<WalletLegacyTransfer>(),
          const std::vector<std::pair<TransactionInformation, int64_t>>& externalTxs = std::vector<std::pair<TransactionInformation, int64_t>>());

  System::Dispatcher dispatcher;
  Logging::ConsoleLogger logger;
  CryptoNote::Currency currency;
  TestBlockchainGenerator generator;
  INodeTrivialRefreshStub node;
  CryptoNote::WalletGreen alice;
  std::string aliceAddress;

  const uint64_t SENT = 1122334455;
  const uint64_t FEE;
  const std::string RANDOM_ADDRESS = "2634US2FAz86jZT73YmM8u5GPCknT2Wxj8bUCKivYKpThFhF2xsjygMGxbxZzM42zXhKUhym6Yy6qHHgkuWtruqiGkDpX6m";
};

void WalletApi::SetUp() {
  alice.initialize("pass");
  aliceAddress = alice.createAddress();
}

void WalletApi::TearDown() {
  alice.shutdown();
  wait(100); //ObserverManager bug workaround
}

CryptoNote::AccountPublicAddress WalletApi::parseAddress(const std::string& address) {
  CryptoNote::AccountPublicAddress pubAddr;
  if (!currency.parseAccountAddressString(address, pubAddr)) {
    throw std::system_error(std::make_error_code(std::errc::invalid_argument));
  }

  return pubAddr;
}

void WalletApi::generateBlockReward() {
  generateBlockReward(aliceAddress);
}

void WalletApi::generateBlockReward(const std::string& address) {
  generator.getBlockRewardForAddress(parseAddress(address));
}

void WalletApi::unlockMoney() {
  unlockMoney(alice, node);
}

void WalletApi::unlockMoney(CryptoNote::WalletGreen& wallet, INodeTrivialRefreshStub& inode) {
  auto prev = wallet.getActualBalance();
  generator.generateEmptyBlocks(11); //coinbase money should become available after 10 blocks
  inode.updateObservers();
  waitActualBalanceUpdated(wallet, prev);
}

void WalletApi::generateAndUnlockMoney() {
  generateBlockReward();
  unlockMoney();
}

template<typename T>
void WalletApi::waitValueChanged(CryptoNote::WalletGreen& wallet, T prev, std::function<T ()>&& f) {
  while (prev == f()) {
    wallet.getEvent();
  }
}

template<typename T>
void WalletApi::waitForValue(CryptoNote::WalletGreen& wallet, T value, std::function<T ()>&& f) {
  while (value != f()) {
    wallet.getEvent();
  }
}

void WalletApi::waitActualBalanceUpdated() {
  waitActualBalanceUpdated(alice, alice.getActualBalance());
}

void WalletApi::waitActualBalanceUpdated(uint64_t prev) {
  waitActualBalanceUpdated(alice, prev);
}

void WalletApi::waitForActualBalance(uint64_t expected) {
  waitForValue<uint64_t>(alice, expected, [this] () { return this->alice.getActualBalance(); });
}

void WalletApi::waitActualBalanceUpdated(CryptoNote::WalletGreen& wallet, uint64_t prev) {
  waitValueChanged<uint64_t>(wallet, prev, [&wallet] () { return wallet.getActualBalance(); });
}

void WalletApi::waitPendingBalanceUpdated() {
  waitPendingBalanceUpdated(alice, alice.getPendingBalance());
}

void WalletApi::waitPendingBalanceUpdated(uint64_t prev) {
  waitPendingBalanceUpdated(alice, prev);
}

void WalletApi::waitPendingBalanceUpdated(CryptoNote::WalletGreen& wallet, uint64_t prev) {
  waitValueChanged<uint64_t>(wallet, prev, [&wallet] () { return wallet.getPendingBalance(); });
}

void WalletApi::waitForTransactionCount(CryptoNote::WalletGreen& wallet, uint64_t expected) {
  waitForValue<size_t>(wallet, expected, [&wallet] () { return wallet.getTransactionCount(); });
}

void WalletApi::generateAddressesWithPendingMoney(size_t count) {
  for (size_t i = 0; i < count; ++i) {
    generateBlockReward(alice.createAddress());
  }
}

size_t WalletApi::sendMoneyToRandomAddressFrom(const std::string& address, uint64_t amount, uint64_t fee) {
  CryptoNote::WalletTransfer transfer;
  transfer.address = RANDOM_ADDRESS;
  transfer.amount = amount;

  return alice.transfer(address, transfer, fee, 0);
}

size_t WalletApi::sendMoneyToRandomAddressFrom(const std::string& address) {
  return sendMoneyToRandomAddressFrom(address, SENT, FEE);
}

void WalletApi::fillWalletWithDetailsCache() {
  generateAddressesWithPendingMoney(10);
  unlockMoney();

  auto alicePrev = alice.getActualBalance();
  for (size_t i = 1; i < 5; ++i) {
    sendMoneyToRandomAddressFrom(alice.getAddress(i));
  }

  node.updateObservers();
  waitActualBalanceUpdated(alicePrev);

  for (size_t i = 5; i < 10; ++i) {
    sendMoneyToRandomAddressFrom(alice.getAddress(i));
  }
}

size_t WalletApi::sendMoney(const std::string& to, int64_t amount, uint64_t fee, uint64_t mixIn, const std::string& extra, uint64_t unlockTimestamp) {
  CryptoNote::WalletTransfer transfer;
  transfer.address = to;
  transfer.amount = amount;

  return alice.transfer(transfer, fee, mixIn, extra, unlockTimestamp);
}

void WalletApi::wait(uint64_t milliseconds) {
  System::Timer timer(dispatcher);
  timer.sleep(std::chrono::nanoseconds(milliseconds * 1000000));
}

static const uint64_t TEST_BLOCK_REWARD = 70368744177663;

TEST_F(WalletApi, emptyBalance) {
  ASSERT_EQ(0, alice.getActualBalance());
  ASSERT_EQ(0, alice.getPendingBalance());
}

TEST_F(WalletApi, receiveMoneyOneAddress) {
  generateBlockReward();

  auto prev = alice.getPendingBalance();
  node.updateObservers();
  waitPendingBalanceUpdated(prev);

  ASSERT_EQ(0, alice.getActualBalance());
  ASSERT_EQ(TEST_BLOCK_REWARD, alice.getPendingBalance());

  ASSERT_EQ(0, alice.getActualBalance(aliceAddress));
  ASSERT_EQ(TEST_BLOCK_REWARD, alice.getPendingBalance(aliceAddress));
}

TEST_F(WalletApi, unlockMoney) {
  generateAndUnlockMoney();

  ASSERT_EQ(TEST_BLOCK_REWARD, alice.getActualBalance());
  ASSERT_EQ(0, alice.getPendingBalance());
}

TEST_F(WalletApi, transferFromOneAddress) {
  CryptoNote::WalletGreen bob(dispatcher, currency, node);
  bob.initialize("pass2");
  std::string bobAddress = bob.createAddress();

  generateAndUnlockMoney();

  auto alicePrev = alice.getActualBalance();
  sendMoney(bobAddress, SENT, FEE);
  node.updateObservers();
  waitActualBalanceUpdated(alicePrev);
  waitPendingBalanceUpdated(bob, 0);

  ASSERT_EQ(0, bob.getActualBalance());
  ASSERT_EQ(SENT, bob.getPendingBalance());

  ASSERT_EQ(TEST_BLOCK_REWARD - SENT - FEE, alice.getActualBalance() + alice.getPendingBalance());
  ASSERT_EQ(TEST_BLOCK_REWARD - SENT - FEE, alice.getActualBalance(aliceAddress) + alice.getPendingBalance(aliceAddress));

  bob.shutdown();
  wait(100);
}

TEST_F(WalletApi, transferMixin) {
  generateAndUnlockMoney();

  auto alicePrev = alice.getActualBalance();

  ASSERT_NO_THROW(sendMoney(RANDOM_ADDRESS, SENT, FEE, 12));
  node.updateObservers();

  waitActualBalanceUpdated(alicePrev);

  auto tx = alice.getTransaction(0);
  ASSERT_EQ(CryptoNote::WalletTransactionState::SUCCEEDED, tx.state);
}

TEST_F(WalletApi, transferTooBigMixin) {
  generateAndUnlockMoney();

  node.setMaxMixinCount(10);
  ASSERT_ANY_THROW(sendMoney(RANDOM_ADDRESS, SENT, FEE, 15));
}

TEST_F(WalletApi, transferNegativeAmount) {
  generateAndUnlockMoney();
  ASSERT_ANY_THROW(sendMoney(RANDOM_ADDRESS, -static_cast<int64_t>(SENT), FEE));
}

TEST_F(WalletApi, transferFromTwoAddresses) {
  generateBlockReward();
  generateBlockReward(alice.createAddress());
  generator.generateEmptyBlocks(11);
  node.updateObservers();

  waitForActualBalance(2 * TEST_BLOCK_REWARD);

  CryptoNote::WalletGreen bob(dispatcher, currency, node);
  bob.initialize("pass2");
  std::string bobAddress = bob.createAddress();

  const uint64_t sent = 2 * TEST_BLOCK_REWARD - 10 * FEE;

  auto bobPrev = bob.getPendingBalance();
  auto alicePendingPrev = alice.getPendingBalance();
  auto aliceActualPrev = alice.getActualBalance();

  sendMoney(bobAddress, sent, FEE);

  node.updateObservers();

  waitActualBalanceUpdated(aliceActualPrev);
  waitPendingBalanceUpdated(bob, bobPrev);
  waitPendingBalanceUpdated(alicePendingPrev);

  ASSERT_EQ(0, bob.getActualBalance());
  ASSERT_EQ(sent, bob.getPendingBalance());

  ASSERT_EQ(2 * TEST_BLOCK_REWARD - sent - FEE, alice.getActualBalance() + alice.getPendingBalance());

  bob.shutdown();
  wait(100);
}

TEST_F(WalletApi, transferTooBigTransaction) {
  CryptoNote::Currency cur = CryptoNote::CurrencyBuilder(logger).blockGrantedFullRewardZone(5).minerTxBlobReservedSize(2).currency();
  TestBlockchainGenerator gen(cur);
  INodeTrivialRefreshStub n(gen);

  CryptoNote::WalletGreen wallet(dispatcher, cur, n);
  wallet.initialize("pass");
  wallet.createAddress();

  gen.getBlockRewardForAddress(parseAddress(wallet.getAddress(0)));

  auto prev = wallet.getActualBalance();
  gen.generateEmptyBlocks(11);
  n.updateObservers();
  waitActualBalanceUpdated(wallet, prev);

  CryptoNote::WalletTransfer transfer;
  transfer.address = RANDOM_ADDRESS;
  transfer.amount = SENT;

  ASSERT_ANY_THROW(wallet.transfer(transfer, FEE));
}

TEST_F(WalletApi, balanceAfterTransfer) {
  generateAndUnlockMoney();

  sendMoney(RANDOM_ADDRESS, SENT, FEE);

  ASSERT_EQ(TEST_BLOCK_REWARD - SENT - FEE, alice.getActualBalance() + alice.getPendingBalance());
}

TEST_F(WalletApi, specificAddressesBalances) {
  generateAndUnlockMoney();

  auto secondAddress = alice.createAddress();
  generateBlockReward(secondAddress);
  node.updateObservers();
  waitPendingBalanceUpdated();

  ASSERT_EQ(TEST_BLOCK_REWARD, alice.getActualBalance());
  ASSERT_EQ(TEST_BLOCK_REWARD, alice.getActualBalance(aliceAddress));
  ASSERT_EQ(0, alice.getActualBalance(secondAddress));

  ASSERT_EQ(TEST_BLOCK_REWARD, alice.getPendingBalance());
  ASSERT_EQ(TEST_BLOCK_REWARD, alice.getPendingBalance(secondAddress));
  ASSERT_EQ(0, alice.getPendingBalance(aliceAddress));
}

TEST_F(WalletApi, transferFromSpecificAddress) {
  generateBlockReward();

  auto secondAddress = alice.createAddress();
  generateBlockReward(secondAddress);

  generator.generateEmptyBlocks(11);
  node.updateObservers();
  waitActualBalanceUpdated();

  auto prevActual = alice.getActualBalance();
  auto prevPending = alice.getPendingBalance();

  sendMoneyToRandomAddressFrom(secondAddress);

  node.updateObservers();
  waitActualBalanceUpdated(prevActual);
  waitPendingBalanceUpdated(prevPending);

  ASSERT_EQ(TEST_BLOCK_REWARD, alice.getActualBalance(aliceAddress));

  //NOTE: do not expect the rule 'actual + pending == previous - sent - fee' to work,
  //because change is sent to address #0.
  ASSERT_NE(TEST_BLOCK_REWARD, alice.getActualBalance(secondAddress));
  ASSERT_NE(0, alice.getPendingBalance(aliceAddress));
  ASSERT_EQ(2 * TEST_BLOCK_REWARD - SENT - FEE, alice.getActualBalance() + alice.getPendingBalance());
}

TEST_F(WalletApi, loadEmptyWallet) {
  std::stringstream data;
  alice.save(data, true, true);

  WalletGreen bob(dispatcher, currency, node);
  bob.load(data, "pass");

  ASSERT_EQ(alice.getAddressCount(), bob.getAddressCount());
  ASSERT_EQ(alice.getActualBalance(), bob.getActualBalance());
  ASSERT_EQ(alice.getPendingBalance(), bob.getPendingBalance());
  ASSERT_EQ(alice.getTransactionCount(), bob.getTransactionCount());

  bob.shutdown();
  wait(100);
}

TEST_F(WalletApi, loadWalletWithoutAddresses) {
  WalletGreen bob(dispatcher, currency, node);
  bob.initialize("pass");

  std::stringstream data;
  bob.save(data, false, false);
  bob.shutdown();

  WalletGreen carol(dispatcher, currency, node);
  carol.load(data, "pass");

  ASSERT_EQ(0, carol.getAddressCount());
  carol.shutdown();
  wait(100);
}

void compareWalletsAddresses(const CryptoNote::WalletGreen& alice, const CryptoNote::WalletGreen& bob) {
  ASSERT_EQ(alice.getAddressCount(), bob.getAddressCount());
  for (size_t i = 0; i < alice.getAddressCount(); ++i) {
    ASSERT_EQ(alice.getAddress(i), bob.getAddress(i));
  }
}

void compareWalletsActualBalance(const CryptoNote::WalletGreen& alice, const CryptoNote::WalletGreen& bob) {
  ASSERT_EQ(alice.getActualBalance(), bob.getActualBalance());
  for (size_t i = 0; i < bob.getAddressCount(); ++i) {
    auto addr = bob.getAddress(i);
    ASSERT_EQ(alice.getActualBalance(addr), bob.getActualBalance(addr));
  }
}

void compareWalletsPendingBalance(const CryptoNote::WalletGreen& alice, const CryptoNote::WalletGreen& bob) {
  ASSERT_EQ(alice.getPendingBalance(), bob.getPendingBalance());
  for (size_t i = 0; i < bob.getAddressCount(); ++i) {
    auto addr = bob.getAddress(i);
    ASSERT_EQ(alice.getActualBalance(addr), bob.getActualBalance(addr));
  }
}

void compareWalletsTransactionTransfers(const CryptoNote::WalletGreen& alice, const CryptoNote::WalletGreen& bob) {
  ASSERT_EQ(alice.getTransactionCount(), bob.getTransactionCount());
  for (size_t i = 0; i < bob.getTransactionCount(); ++i) {
    ASSERT_EQ(alice.getTransaction(i), bob.getTransaction(i));

    ASSERT_EQ(alice.getTransactionTransferCount(i), bob.getTransactionTransferCount(i));

    size_t trCount = bob.getTransactionTransferCount(i);
    for (size_t j = 0; j < trCount; ++j) {
      ASSERT_EQ(alice.getTransactionTransfer(i, j), bob.getTransactionTransfer(i, j));
    }
  }
}

TEST_F(WalletApi, loadCacheDetails) {
  fillWalletWithDetailsCache();

  std::stringstream data;
  alice.save(data, true, true);

  WalletGreen bob(dispatcher, currency, node);
  bob.load(data, "pass");

  compareWalletsAddresses(alice, bob);
  compareWalletsActualBalance(alice, bob);
  compareWalletsPendingBalance(alice, bob);
  compareWalletsTransactionTransfers(alice, bob);

  bob.shutdown();
  wait(100); //ObserverManager bug workaround
}

TEST_F(WalletApi, loadNoCacheNoDetails) {
  fillWalletWithDetailsCache();

  std::stringstream data;
  alice.save(data, false, false);

  WalletGreen bob(dispatcher, currency, node);
  bob.load(data, "pass");

  compareWalletsAddresses(alice, bob);

  ASSERT_EQ(0, bob.getActualBalance());
  ASSERT_EQ(0, bob.getPendingBalance());
  ASSERT_EQ(0, bob.getTransactionCount());

  bob.shutdown();
  wait(100);
}

TEST_F(WalletApi, loadNoCacheDetails) {
  fillWalletWithDetailsCache();

  std::stringstream data;
  alice.save(data, true, false);

  WalletGreen bob(dispatcher, currency, node);
  bob.load(data, "pass");

  compareWalletsAddresses(alice, bob);

  ASSERT_EQ(0, bob.getActualBalance());
  ASSERT_EQ(0, bob.getPendingBalance());

  compareWalletsTransactionTransfers(alice, bob);

  bob.shutdown();
  wait(100);
}

TEST_F(WalletApi, loadCacheNoDetails) {
  fillWalletWithDetailsCache();

  std::stringstream data;
  alice.save(data, false, true);

  WalletGreen bob(dispatcher, currency, node);
  bob.load(data, "pass");

  compareWalletsAddresses(alice, bob);
  compareWalletsActualBalance(alice, bob);
  compareWalletsPendingBalance(alice, bob);

  ASSERT_EQ(0, bob.getTransactionCount());

  bob.shutdown();
  wait(100);
}

TEST_F(WalletApi, loadWithWrongPassword) {
  std::stringstream data;
  alice.save(data, false, false);

  WalletGreen bob(dispatcher, currency, node);
  ASSERT_ANY_THROW(bob.load(data, "pass2"));
}

void WalletApi::testIWalletDataCompatibility(bool details, const std::string& cache, const std::vector<WalletLegacyTransaction>& txs,
    const std::vector<WalletLegacyTransfer>& trs, const std::vector<std::pair<TransactionInformation, int64_t>>& externalTxs) {
  CryptoNote::AccountBase account;
  account.generate();

  WalletUserTransactionsCache iWalletCache;
  WalletLegacySerializer walletSerializer(account, iWalletCache);

  for (const auto& tx: txs) {
    std::vector<WalletLegacyTransfer> txtrs;
    if (tx.firstTransferId != WALLET_LEGACY_INVALID_TRANSFER_ID && tx.transferCount != 0) {
      for (size_t i = tx.firstTransferId; i < (tx.firstTransferId  + tx.transferCount); ++i) {
        txtrs.push_back(trs[i]);
      }
    }
    auto txId = iWalletCache.addNewTransaction(tx.totalAmount, tx.fee, tx.extra, txtrs, tx.unlockTime);
    iWalletCache.updateTransactionSendingState(txId, std::error_code());
  }

  for (const auto& item: externalTxs) {
    iWalletCache.onTransactionUpdated(item.first, item.second);
  }

  std::stringstream stream;
  walletSerializer.serialize(stream, "pass", details, std::string());

  WalletGreen wallet(dispatcher, currency, node);
  wallet.load(stream, "pass");

  EXPECT_EQ(1, wallet.getAddressCount());

  AccountPublicAddress addr;
  currency.parseAccountAddressString(wallet.getAddress(0), addr);
  EXPECT_EQ(account.getAccountKeys().address.spendPublicKey, addr.spendPublicKey);
  EXPECT_EQ(account.getAccountKeys().address.viewPublicKey, addr.viewPublicKey);
  EXPECT_EQ(0, wallet.getActualBalance());
  EXPECT_EQ(0, wallet.getPendingBalance());

  if (details) {
    auto outcomingTxCount = wallet.getTransactionCount() - externalTxs.size();
    ASSERT_EQ(txs.size(), outcomingTxCount);
    for (size_t i = 0; i < outcomingTxCount; ++i) {
      auto tx = wallet.getTransaction(i);
      EXPECT_EQ(WALLET_LEGACY_UNCONFIRMED_TRANSACTION_HEIGHT, tx.blockHeight);
      EXPECT_EQ(txs[i].extra, tx.extra);
      EXPECT_EQ(txs[i].fee, tx.fee);
      EXPECT_EQ(WalletTransactionState::SUCCEEDED, tx.state);
      EXPECT_EQ(-txs[i].totalAmount, tx.totalAmount);
      EXPECT_EQ(txs[i].unlockTime, tx.unlockTime);

      size_t trsCount = wallet.getTransactionTransferCount(i);
      ASSERT_EQ(txs[i].transferCount, trsCount);
      for (size_t j = 0; j < trsCount; ++j) {
        ASSERT_NE(WALLET_LEGACY_INVALID_TRANSFER_ID, txs[i].firstTransferId);

        size_t index = txs[i].firstTransferId + j;
        EXPECT_EQ(trs[index].address, wallet.getTransactionTransfer(i, j).address);
        EXPECT_EQ(trs[index].amount, wallet.getTransactionTransfer(i, j).amount);
      }
    }

    ASSERT_EQ(txs.size() + externalTxs.size(), wallet.getTransactionCount());
    for (size_t i = outcomingTxCount; i < wallet.getTransactionCount(); ++i) {
      auto inTx = externalTxs[i - outcomingTxCount].first;
      auto txBalance = externalTxs[i - outcomingTxCount].second;
      auto tx = wallet.getTransaction(i);

      EXPECT_EQ(inTx.blockHeight, tx.blockHeight);
      EXPECT_EQ(0, tx.creationTime);
      std::string extraString(inTx.extra.begin(), inTx.extra.end());
      EXPECT_EQ(extraString, tx.extra);
      EXPECT_EQ(txBalance, tx.totalAmount);

      if (inTx.totalAmountIn) {
        EXPECT_EQ(inTx.totalAmountIn - inTx.totalAmountOut, tx.fee);
      } else {
        EXPECT_EQ(0, tx.fee);
      }

      EXPECT_EQ(inTx.transactionHash, tx.hash);
      EXPECT_EQ(WalletTransactionState::SUCCEEDED, tx.state);
      EXPECT_EQ(inTx.unlockTime, tx.unlockTime);
    }
  } else {
    EXPECT_EQ(0, wallet.getTransactionCount());
  }
}

TEST_F(WalletApi, IWalletDataCompatibilityEmptyDetailsNoCache) {
  testIWalletDataCompatibility(true);
}

TEST_F(WalletApi, IWalletDataCompatibilityEmptyNoDetailsNoCache) {
  testIWalletDataCompatibility(false);
}

TEST_F(WalletApi, IWalletDataCompatibilityEmptyNoDetailsCache) {
  std::string cache(1024, 'c');
  testIWalletDataCompatibility(false, cache);
}

TEST_F(WalletApi, IWalletDataCompatibilityEmptyDetailsCache) {
  std::string cache(1024, 'c');
  testIWalletDataCompatibility(true, cache);
}

TEST_F(WalletApi, IWalletDataCompatibilityDetails) {
  std::vector<WalletLegacyTransaction> txs;

  WalletLegacyTransaction tx1;
  tx1.firstTransferId = 0;
  tx1.transferCount = 2;
  tx1.unlockTime = 12;
  tx1.totalAmount = 1234567890;
  tx1.timestamp = (uint64_t) 8899007711;
  tx1.extra = "jsjeokvsnxcvkhdoifjaslkcvnvuergeonlsdnlaksmdclkasowehunkjn";
  tx1.fee = 1000;
  tx1.isCoinbase = false;
  txs.push_back(tx1);

  std::vector<WalletLegacyTransfer> trs;

  WalletLegacyTransfer tr1;
  tr1.address = RANDOM_ADDRESS;
  tr1.amount = SENT;
  trs.push_back(tr1);

  WalletLegacyTransfer tr2;
  tr2.amount = 102034;
  tr2.address = alice.getAddress(0);
  trs.push_back(tr2);

  std::vector<std::pair<TransactionInformation, int64_t>> incomingTxs;

  TransactionInformation iTx1;
  iTx1.timestamp = 929453;
  iTx1.totalAmountIn = 200353;
  iTx1.blockHeight = 2349;
  std::iota(iTx1.transactionHash.data, iTx1.transactionHash.data+32, 125);
  iTx1.extra = {1,2,3,4,5,6,7,8,9,1,2,3,4,5,6,7,8,9};
  std::iota(iTx1.publicKey.data, iTx1.publicKey.data+32, 15);
  iTx1.totalAmountOut = 948578;
  iTx1.unlockTime = 17;
  incomingTxs.push_back(std::make_pair(iTx1, 99874442));

  TransactionInformation iTx2;
  iTx2.timestamp = 10010;
  iTx2.totalAmountIn = 0;
  iTx2.blockHeight = 2350;
  std::iota(iTx2.transactionHash.data, iTx2.transactionHash.data+32, 15);
  iTx2.extra = {11,22,33,44,55,66,77,88,99,12,13,14,15,16};
  std::iota(iTx2.publicKey.data, iTx2.publicKey.data+32, 5);
  iTx2.totalAmountOut = 99874442;
  iTx2.unlockTime = 12;
  incomingTxs.push_back(std::make_pair(iTx2, 99874442));

  std::string cache(1024, 'c');
  testIWalletDataCompatibility(true, cache, txs, trs, incomingTxs);
}

TEST_F(WalletApi, getEventStopped) {
  alice.stop();
  ASSERT_ANY_THROW(alice.getEvent());
}

TEST_F(WalletApi, stopStart) {
  alice.stop();
  alice.start();

  ASSERT_NO_THROW(alice.getActualBalance());
}

TEST_F(WalletApi, uninitializedObject) {
  WalletGreen bob(dispatcher, currency, node);

  ASSERT_ANY_THROW(bob.changePassword("s", "p"));
  std::stringstream stream;
  ASSERT_ANY_THROW(bob.save(stream));
  ASSERT_ANY_THROW(bob.getAddressCount());
  ASSERT_ANY_THROW(bob.getAddress(0));
  ASSERT_ANY_THROW(bob.createAddress());
  ASSERT_ANY_THROW(bob.deleteAddress(RANDOM_ADDRESS));
  ASSERT_ANY_THROW(bob.getActualBalance());
  ASSERT_ANY_THROW(bob.getActualBalance(RANDOM_ADDRESS));
  ASSERT_ANY_THROW(bob.getPendingBalance());
  ASSERT_ANY_THROW(bob.getPendingBalance(RANDOM_ADDRESS));
  ASSERT_ANY_THROW(bob.getTransactionCount());
  ASSERT_ANY_THROW(bob.getTransaction(0));
  ASSERT_ANY_THROW(bob.getTransactionTransferCount(0));
  ASSERT_ANY_THROW(bob.getTransactionTransfer(0, 0));
  ASSERT_ANY_THROW(sendMoneyToRandomAddressFrom(aliceAddress));
  ASSERT_ANY_THROW(bob.shutdown());
  wait(100);
}

const size_t TX_PUB_KEY_EXTRA_SIZE = 33;

TEST_F(WalletApi, checkSentTransaction) {
  generateAndUnlockMoney();
  size_t txId = sendMoney(RANDOM_ADDRESS, SENT, FEE);

  CryptoNote::WalletTransaction tx = alice.getTransaction(txId);
  ASSERT_EQ(CryptoNote::WalletTransactionState::SUCCEEDED, tx.state);
  ASSERT_EQ(0, tx.timestamp);
  ASSERT_EQ(CryptoNote::WALLET_LEGACY_UNCONFIRMED_TRANSACTION_HEIGHT, tx.blockHeight);
  ASSERT_EQ(-static_cast<int64_t>(SENT + FEE), tx.totalAmount);
  ASSERT_EQ(FEE, tx.fee);
  ASSERT_EQ(0, tx.unlockTime);
  ASSERT_EQ(TX_PUB_KEY_EXTRA_SIZE, tx.extra.size()); //Transaction public key only
}

std::string removeTxPublicKey(const std::string& txExtra) {
  if (txExtra.size() <= TX_PUB_KEY_EXTRA_SIZE) {
    return std::string();
  }

  return txExtra.substr(TX_PUB_KEY_EXTRA_SIZE);
}

std::string createExtraNonce(const std::string& nonce) {
  CryptoNote::TransactionExtra txExtra;
  CryptoNote::TransactionExtraNonce extraNonce;
  extraNonce.nonce = asBinaryArray(nonce);
  txExtra.set(extraNonce);
  auto vec = txExtra.serialize();
  return std::string(vec.begin(), vec.end());
}

TEST_F(WalletApi, checkSentTransactionWithExtra) {
  const std::string extra = createExtraNonce("\x01\x23\x45\x67\x89\xab\xcd\xef");

  generateAndUnlockMoney();
  size_t txId = sendMoney(RANDOM_ADDRESS, SENT, FEE, 0, extra);

  CryptoNote::WalletTransaction tx = alice.getTransaction(txId);
  ASSERT_EQ(CryptoNote::WalletTransactionState::SUCCEEDED, tx.state);
  ASSERT_EQ(0, tx.timestamp);
  ASSERT_EQ(CryptoNote::WALLET_LEGACY_UNCONFIRMED_TRANSACTION_HEIGHT, tx.blockHeight);
  ASSERT_EQ(-static_cast<int64_t>(SENT + FEE), tx.totalAmount);
  ASSERT_EQ(FEE, tx.fee);
  ASSERT_EQ(0, tx.unlockTime);
  ASSERT_EQ(extra, removeTxPublicKey(tx.extra));
}

TEST_F(WalletApi, checkFailedTransaction) {
  generateAndUnlockMoney();

  node.setNextTransactionError();
  ASSERT_ANY_THROW(sendMoney(RANDOM_ADDRESS, SENT, FEE));

  auto tx = alice.getTransaction(alice.getTransactionCount() - 1);
  ASSERT_EQ(CryptoNote::WalletTransactionState::FAILED, tx.state);
}

TEST_F(WalletApi, checkIncomingTransaction) {
  const std::string extra = createExtraNonce("\x01\x23\x45\x67\x89\xab\xcd\xef");

  generateAndUnlockMoney();

  CryptoNote::WalletGreen bob(dispatcher, currency, node);
  bob.initialize("pass2");
  std::string bobAddress = bob.createAddress();

  sendMoney(bobAddress, SENT, FEE, 0, extra, 11);
  node.updateObservers();
  waitPendingBalanceUpdated(bob, 0);

  auto tx = bob.getTransaction(bob.getTransactionCount() - 1);

  bob.shutdown();
  wait(100); //observer manager bug

  ASSERT_EQ(CryptoNote::WalletTransactionState::SUCCEEDED, tx.state);
  ASSERT_NE(0, tx.timestamp);
  ASSERT_EQ(generator.getBlockchain().size() - 1, tx.blockHeight);
  ASSERT_EQ(SENT, tx.totalAmount);
  ASSERT_EQ(FEE, tx.fee);
  ASSERT_EQ(11, tx.unlockTime);
  ASSERT_EQ(extra, removeTxPublicKey(tx.extra));
}

TEST_F(WalletApi, notEnoughMoney) {
  generateAndUnlockMoney();
  ASSERT_ANY_THROW(sendMoney(RANDOM_ADDRESS, TEST_BLOCK_REWARD, FEE));
}

TEST_F(WalletApi, changePassword) {
  generateAndUnlockMoney();

  ASSERT_NO_THROW(alice.changePassword("pass", "pass2"));

  std::stringstream data;
  alice.save(data, false, false);

  CryptoNote::WalletGreen bob(dispatcher, currency, node);
  ASSERT_NO_THROW(bob.load(data, "pass2"));

  bob.shutdown();
  wait(100);
}

TEST_F(WalletApi, changePasswordWrong) {
  ASSERT_ANY_THROW(alice.changePassword("pass2", "pass3"));
}

TEST_F(WalletApi, shutdownInit) {
  generateBlockReward();
  node.updateObservers();
  waitPendingBalanceUpdated(0);

  alice.shutdown();
  alice.initialize("p");

  EXPECT_EQ(0, alice.getAddressCount());
  EXPECT_EQ(0, alice.getActualBalance());
  EXPECT_EQ(0, alice.getPendingBalance());
}

TEST_F(WalletApi, detachBlockchain) {
  generateAndUnlockMoney();

  auto alicePrev = alice.getActualBalance();

  node.startAlternativeChain(1);
  generator.generateEmptyBlocks(11);
  node.updateObservers();
  waitActualBalanceUpdated(alicePrev);

  ASSERT_EQ(0, alice.getActualBalance());
  ASSERT_EQ(0, alice.getPendingBalance());
}

TEST_F(WalletApi, deleteAddresses) {
  fillWalletWithDetailsCache();
  alice.createAddress();

  for (size_t i = 0; i < 11; ++i) {
    alice.deleteAddress(alice.getAddress(0));
  }

  EXPECT_EQ(0, alice.getActualBalance());
  EXPECT_EQ(0, alice.getPendingBalance());
}

TEST_F(WalletApi, incomingTxTransfer) {
  generateAndUnlockMoney();

  CryptoNote::WalletGreen bob(dispatcher, currency, node);
  bob.initialize("pass2");
  bob.createAddress();
  bob.createAddress();

  sendMoney(bob.getAddress(0), SENT, FEE);
  sendMoney(bob.getAddress(1), 2 * SENT, FEE);
  node.updateObservers();
  waitForTransactionCount(bob, 2);

  EXPECT_EQ(1, bob.getTransactionTransferCount(0));
  ASSERT_EQ(1, bob.getTransactionTransferCount(1));

  auto tr1 = bob.getTransactionTransfer(0, 0);
  EXPECT_EQ(tr1.address, bob.getAddress(0));
  EXPECT_EQ(tr1.amount, SENT);

  auto tr2 = bob.getTransactionTransfer(1, 0);
  EXPECT_EQ(tr2.address, bob.getAddress(1));
  EXPECT_EQ(tr2.amount, 2 * SENT);

  bob.shutdown();
  wait(100);
}

TEST_F(WalletApi, hybridTxTransfer) {
  generateAndUnlockMoney();

  alice.createAddress();
  alice.createAddress();

  CryptoNote::WalletTransfer tr1 { alice.getAddress(1), static_cast<int64_t>(SENT) };
  CryptoNote::WalletTransfer tr2 { alice.getAddress(2), static_cast<int64_t>(2 * SENT) };

  alice.transfer({tr1, tr2}, FEE);
  node.updateObservers();
  dispatcher.yield();

  ASSERT_EQ(2, alice.getTransactionTransferCount(1));

  EXPECT_EQ(tr1.address, alice.getTransactionTransfer(1, 0).address);
  EXPECT_EQ(-tr1.amount, alice.getTransactionTransfer(1, 0).amount);

  EXPECT_EQ(tr2.address, alice.getTransactionTransfer(1, 1).address);
  EXPECT_EQ(-tr2.amount, alice.getTransactionTransfer(1, 1).amount);
}

TEST_F(WalletApi, doubleSpendJustSentOut) {
  generator.getSingleOutputTransaction(parseAddress(aliceAddress), SENT + FEE);
  unlockMoney();

  sendMoney(RANDOM_ADDRESS, SENT, FEE);
  ASSERT_ANY_THROW(sendMoney(RANDOM_ADDRESS, SENT, FEE));
}

TEST_F(WalletApi, syncAfterLoad) {
  std::stringstream data;
  alice.save(data, true, true);
  alice.shutdown();

  generateBlockReward();
  generator.generateEmptyBlocks(11);

  alice.load(data, "pass");

  wait(300);

  ASSERT_EQ(TEST_BLOCK_REWARD, alice.getActualBalance());
}

class INodeNoRelay : public INodeTrivialRefreshStub {
public:
  INodeNoRelay(TestBlockchainGenerator& generator) : INodeTrivialRefreshStub(generator) {}

  virtual void relayTransaction(const CryptoNote::Transaction& transaction, const Callback& callback) {
    m_asyncCounter.addAsyncContext();
    std::thread task(&INodeNoRelay::doNoRelayTransaction, this, transaction, callback);
    task.detach();
  }

  void doNoRelayTransaction(const CryptoNote::Transaction& transaction, const Callback& callback)
  {
    callback(std::error_code());
    m_asyncCounter.delAsyncContext();
  }
};

TEST_F(WalletApi, DISABLED_loadTest) {
  using namespace std::chrono;

  INodeNoRelay noRelayNode(generator);
  CryptoNote::WalletGreen wallet(dispatcher, currency, noRelayNode);
  wallet.initialize("pass");

  const size_t ADDRESSES_COUNT = 1000;

  std::cout << "creating addresses" << std::endl;
  steady_clock::time_point start = steady_clock::now();

  for (size_t i = 0; i < ADDRESSES_COUNT; ++i) {
    wallet.createAddress();
  }

  steady_clock::time_point end = steady_clock::now();
  std::cout << "addresses creation finished in: " << duration_cast<milliseconds>(end - start).count() << " ms" << std::endl;
  std::cout << "filling up the wallets" << std::endl;

  for (size_t i = 0; i < ADDRESSES_COUNT; ++i) {
    if (!(i % 100)) {
      std::cout << "filling " << i << "th wallet" << std::endl;
    }
    generator.generateTransactionsInOneBlock(parseAddress(wallet.getAddress(i)), 10);
    generator.generateTransactionsInOneBlock(parseAddress(wallet.getAddress(i)), 10);
    generator.generateTransactionsInOneBlock(parseAddress(wallet.getAddress(i)), 10);
    generator.generateTransactionsInOneBlock(parseAddress(wallet.getAddress(i)), 10);
    generator.generateTransactionsInOneBlock(parseAddress(wallet.getAddress(i)), 10);
  }

  std::cout << "wallets filled. input any character" << std::endl;
  char x;
  std::cin >> x;

  std::cout << "sync start" << std::endl;
  steady_clock::time_point syncStart = steady_clock::now();
  noRelayNode.updateObservers();
  waitForTransactionCount(wallet, ADDRESSES_COUNT * 50);
  steady_clock::time_point syncEnd = steady_clock::now();
  std::cout << "sync took: " << duration_cast<milliseconds>(syncEnd - syncStart).count() << " ms" << std::endl;

  unlockMoney(wallet, noRelayNode);

  const size_t TRANSACTIONS_COUNT = 1000;
  std::cout << "wallets filled. input any character" << std::endl;
  std::cin >> x;

  steady_clock::time_point transferStart = steady_clock::now();
  for (size_t i = 0; i < TRANSACTIONS_COUNT; ++i) {
    CryptoNote::WalletTransfer tr;
    tr.amount = SENT;
    tr.address = RANDOM_ADDRESS;
    wallet.transfer(tr, FEE);
  }
  steady_clock::time_point transferEnd = steady_clock::now();
  std::cout << "transfers took: " << duration_cast<milliseconds>(transferEnd - transferStart).count() << " ms" << std::endl;

  wallet.shutdown();
  wait(100);
}

TEST_F(WalletApi, transferSmallFeeTransactionThrows) {
  generateAndUnlockMoney();

  ASSERT_ANY_THROW(sendMoneyToRandomAddressFrom(alice.getAddress(0), SENT, currency.minimumFee() - 1));
}