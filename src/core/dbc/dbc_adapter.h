#ifndef DBC_ADAPTER_H
#define DBC_ADAPTER_H

#include "core/dbcdata.h"
#include <QString>

/**
 * @brief DBC 解析适配层
 *
 * 将 DBC 文件解析为 DbcFile 数据结构。
 * 解析算法参考 dbcppp (github.com/xR3b0rn/dbcppp) 的 DBC 语法规则，
 * 支持: VERSION, NS_, BS_, BU_, BO_, SG_, CM_, BA_DEF_, BA_DEF_DEF_,
 *       BA_, VAL_, VAL_TABLE_, SIG_VALTYPE_, BO_TX_BU_
 */

namespace dbc {

/// 解析 DBC 文件，填充 out 结构
/// @return 成功返回 true
bool parse(const QString &filePath, DbcFile &out);

/// 后处理：关联节点收发关系、应用属性值
void postProcess(DbcFile &file);

} // namespace dbc

#endif // DBC_ADAPTER_H
