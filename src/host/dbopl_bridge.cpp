#include "dbopl_bridge.h"
#include "dbopl.h"
#include <mutex>
#include <new>

struct dbopl { DBOPL::Chip chip; };

dbopl_t *dbopl_create(uint32_t rate)
{
    if (!rate) return nullptr;
    static std::once_flag tables;
    std::call_once(tables, DBOPL::InitTables);
    dbopl_t *result = new (std::nothrow) dbopl_t;
    if (result) result->chip.Setup(rate);
    return result;
}
void dbopl_destroy(dbopl_t *chip) { delete chip; }
void dbopl_write(dbopl_t *chip, uint8_t reg, uint8_t value)
{
    chip->chip.WriteReg(reg, value);
}
void dbopl_generate(dbopl_t *chip, int32_t *out, size_t frames)
{
    while (frames) {
        size_t n = frames > 512 ? 512 : frames;
        chip->chip.GenerateBlock2(n, out);
        out += n; frames -= n;
    }
}
