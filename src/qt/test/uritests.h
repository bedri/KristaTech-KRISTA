// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_TEST_URITESTS_H
#define BITCOIN_QT_TEST_URITESTS_H

#include <QObject>
#include <QTest>

class URITests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void uriTests();
    void transactionRecordTests();
    void taprootAndPsbtUiTests();
    void wizardAndConsolidationTests();
    void visualDesignerLifecycleTests();
    void securityUiHardeningTests();
    void faqAndAboutUiTests();
    void topBarMoreMenuAndPinOptionTests();
};

#endif // BITCOIN_QT_TEST_URITESTS_H
