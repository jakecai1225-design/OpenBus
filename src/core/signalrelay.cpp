#include "signalrelay.h"

SignalRelay::SignalRelay(QObject *parent)
    : QObject(parent)
{
}

void SignalRelay::fire()
{
    if (fire0) fire0();
}

void SignalRelay::fireBoolQString(bool a, const QString &b)
{
    if (fnBoolString) fnBoolString(a, b);
}

void SignalRelay::fireQString(const QString &a)
{
    if (fnString) fnString(a);
}

void SignalRelay::fireQStringInt(const QString &a, int b)
{
    if (fnStringInt) fnStringInt(a, b);
}

void SignalRelay::fireIntInt(int a, int b)
{
    if (fnIntInt) fnIntInt(a, b);
}
