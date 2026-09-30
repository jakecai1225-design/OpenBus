#pragma once

#include <QSet>
#include <QString>

/**
 * Single source of truth for which Python plugins appear on the product surface
 * (sidebar installed list / PluginManager discovery of suites / Marketplace cards).
 *
 * Keep: domain suites (*-suite / *-studio) + AI Agent.
 * Retired mixed hubs live under plugins/_retired/ and must never reappear via
 * stale market.json, remote catalogs, or build/bin/plugins extracts.
 */
inline const QSet<QString> &domainPluginAllowlist()
{
    static const QSet<QString> kIds = {
        QStringLiteral("uds-suite"),
        QStringLiteral("dbc-studio"),
        QStringLiteral("eds-studio"),
        QStringLiteral("canopen-suite"),
        QStringLiteral("j1939-suite"),
        QStringLiteral("obd-suite"),
        QStringLiteral("autosar-suite"),
        QStringLiteral("ethercat-suite"),
        QStringLiteral("ai-agent"),
    };
    return kIds;
}

inline const QSet<QString> &retiredPluginIds()
{
    static const QSet<QString> kIds = {
        QStringLiteral("tx-lab"),
        QStringLiteral("bus-security"),
        QStringLiteral("protocol-hub"),
        QStringLiteral("log-analysis"),
        QStringLiteral("bus-utilities"),
    };
    return kIds;
}

inline bool isProductSurfacePlugin(const QString &id)
{
    return domainPluginAllowlist().contains(id);
}
