// Inside protodec.cpp, at the start of void toJson(Field *field, std::ostream &os, bool showType)

// 1. Check for bcl.DateTime signature
Field* dtVal = field->getSubField(1, WIRETYPE_VARINT, 0);
Field* dtScale = field->getSubField(2, WIRETYPE_VARINT, 0);
if (dtVal && dtScale)
{
    int64_t rawVal = 0, scaleVal = 0;
    getInt64(&dtVal->value, &rawVal, 0);
    getInt64(&dtScale->value, &scaleVal, 0);

    if (scaleVal == 0) // DAYS (e.g. 2026-09-11)
    {
        time_t unixSeconds = rawVal * 86400;
        struct tm* timeinfo = gmtime(&unixSeconds);
        char buffer[16];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d", timeinfo);
        os << "\"" << buffer << "\"";
        return;
    }
    else if (scaleVal == 5) // TICKS (e.g. 2026-09-11T11:26:34.7483647)
    {
        time_t unixSeconds = rawVal / 10000000;
        int64_t remainderTicks = rawVal % 10000000;
        if (remainderTicks < 0) remainderTicks += 10000000;

        struct tm* timeinfo = gmtime(&unixSeconds);
        char baseTime[32];
        strftime(baseTime, sizeof(baseTime), "%Y-%m-%dT%H:%M:%S", timeinfo);

        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%s.%07lld", baseTime, (long long)remainderTicks);
        os << "\"" << buffer << "\"";
        return;
    }
}

// 2. Check for bcl.Decimal signature (Amounts like 29.07)
Field* decVal = field->getSubField(1, WIRETYPE_VARINT, 0);
Field* decSignScale = field->getSubField(3, WIRETYPE_VARINT, 0);
if (decVal && decSignScale && !dtVal)
{
    uint64_t pence = 0;
    int64_t signScale = 0;
    getUint64(&decVal->value, &pence, 0);
    getInt64(&decSignScale->value, &signScale, 0);

    int scale = (signScale >> 1) & 0xFFFF;
    bool isNegative = (signScale & 1) != 0;

    double divisor = 1.0;
    for (int i = 0; i < scale; i++) divisor *= 10.0;

    double amount = (double)pence / divisor;
    if (isNegative) amount = -amount;

    os << amount;
    return;
}
