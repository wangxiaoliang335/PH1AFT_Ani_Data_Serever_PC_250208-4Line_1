# PLC通信协议详细字段说明（实际使用部分）

> **说明**：本文档仅包含本工程代码中实际使用的PLC通信协议，基于代码分析生成。

---

## 一、通信配置 (MNetH.ini)

### 基本配置参数
| 字段 | 类型 | 说明 | 默认值 |
|------|------|------|--------|
| Chanel | int | 通信通道号 | 151 |
| Local | int | 本地站号 | 3 |
| Unit | int | 单元号 | 1 |
| PrevLocal | int | 前站站号 | 2 |
| NextLocal | int | 后站站号 | 4 |
| UseDialog | int | 是否使用对话框 | 1 |
| ShowDialog | int | 是否显示对话框 | 0 |
| Network | int | 网络号 | 0 |

### 超时配置
| 字段 | 类型 | 说明 | 默认值(ms) |
|------|------|------|-----------|
| T1 | int | 超时值1 | 200 |
| T2 | int | 超时值2 | 30000 |
| T3 | int | 超时值3 | 200 |
| DefaultTimeOut | int | 默认超时 | 10000 |

---

## 二、实际使用的数据结构

### 1. JobData (作业数据) - 64个Word

> **说明**：PLC端分配64个Word，实际使用59个Word（偏移0-58），剩余5个Word为预留空间。

**实际使用位置：**
- `PlcThread.cpp::JobDataStart()` - 读取作业数据
- `MNetH::GetJobData(eWordType_JobData1 + iNum, &pJobData)` - AOI/Gamma系统
- `MNetH::GetJobData(eWordType_UnloadJobData1 + iNum, &pJobData)` - ULD系统

| 字段名 | 类型 | 偏移 | 长度(Word) | 说明 | 代码使用 |
|--------|------|------|-----------|------|---------|
| Casssette_Sequence_No | USHORT | 0 | 1 | 载具序列号 | ✓ 使用 |
| Job_Sequence_No | USHORT | 1 | 1 | 作业序列号 | ✓ 使用 |
| Group_Index | USHORT | 2 | 1 | 组索引 | ✓ 使用 |
| Product_Type | USHORT | 3 | 1 | 产品类型 | ✓ 使用 |
| CST_Operation_Mode | USHORT | 4 | 1 | 载具操作模式（二进制位字段）<br>Bit[0-1]: CST操作模式<br>Bit[2-3]: 基板类型<br>Bit[4]: CIM模式<br>Bit[5-9]: 作业类型<br>Bit[10-12]: 作业判定<br>Bit[13]: 采样槽标志<br>Bit[14]: 首次运行标志 | ✓ 使用（解析为二进制） |
| Job_Grade | USHORT | 5 | 1 | 作业等级 | ✓ 使用 |
| Job_ID | char[20] | 6 | 10 | 作业ID（20字节ASCII） | ✓ 使用 |
| INSP_Reservation | USHORT | 16 | 1 | 检查预约（二进制位字段）<br>Bit[0]: INSP预约<br>Bit[1]: EQP预约<br>Bit[2]: LastGlass标志 | ✓ 使用（解析为二进制） |
| InspJudge_Data | USHORT[2] | 17 | 2 | 检查判定数据 | ✓ 使用 |
| Tracking_Data | USHORT[2] | 19 | 2 | 跟踪数据 | ✓ 使用 |
| EQP_Flag | USHORT[2] | 21 | 2 | 设备标志 | ✓ 使用 |
| Chip_Count | USHORT | 23 | 1 | 芯片数量 | ✓ 使用 |
| PP_ID | char[26] | 24 | 13 | PP ID（26字节ASCII） | ✓ 使用 |
| FPC_ID | char[40] | 37 | 20 | FPC ID（40字节ASCII） | ✓ 使用 |
| Cassette_Setting_Code | char[4] | 57 | 2 | 载具设置代码（4字节ASCII） | ✓ 使用 |

**PLC地址映射：**
- AOI/Gamma: `eWordType_JobData1` (对应 `LOCAL_WORD_L2M_JOB_DATA1` = 0x1822) + Panel编号
- ULD: `eWordType_UnloadJobData1` (对应 `LOCAL_WORD_L2M_UNLOAD_JOB_DATA1` = 0x1A40) + Panel编号

**触发Bit：**
- `eBitType_JobDataStart1` + Panel编号 (0x0706) - PLC设置，PC读取后设置 `eBitType_JobDataEnd1` (0x1716)
- `eBitType_UnloadJobDataStart1` + Panel编号 (0x070A) - PLC设置，PC读取后设置 `eBitType_UnloadJobDataEnd1` (0x171A)

---

### 2. DfsData (DFS数据) - 58个Word

**实际使用位置：**
- `PlcThread.cpp::SumDFSDataStart()` - 读取DFS数据用于报工
- `MNetH::GetDfsData(eWordType_DFSValue1 + iNum, &pDfsData)` - AOI系统
- `MNetH::GetDfsData(eWordType_UnloadOKDFSValue1 + iNum, &pDfsData)` - ULD OK
- `MNetH::GetDfsData(eWordType_UnloadNGDFSValue1 + iNum, &pDfsData)` - ULD NG
- `MNetH::GetDfsData(eWordType_GammaNGDFSValue1 + iNum, &pDfsData)` - Gamma NG

| 字段名 | 类型 | 偏移 | 长度(Word) | 说明 | 代码使用 |
|--------|------|------|-----------|------|---------|
| m_StartTime1 | USHORT | 0 | 1 | 开始时间1（年/月/日） | ✓ 使用 |
| m_StartTime2 | USHORT | 1 | 1 | 开始时间2（时/分/秒） | ✓ 使用 |
| m_StartTime3 | USHORT | 2 | 1 | 开始时间3（毫秒高位） | ✓ 使用 |
| m_LoadHandlerTime1 | USHORT | 3 | 1 | 上料Handler时间1 | ✓ 使用 |
| m_LoadHandlerTime2 | USHORT | 4 | 1 | 上料Handler时间2 | ✓ 使用 |
| m_LoadHandlerTime3 | USHORT | 5 | 1 | 上料Handler时间3 | ✓ 使用 |
| m_UnLoadHandlerTime1 | USHORT | 6 | 1 | 下料Handler时间1 | ✓ 使用 |
| m_UnLoadHandlerTime2 | USHORT | 7 | 1 | 下料Handler时间2 | ✓ 使用 |
| m_UnLoadHandlerTime3 | USHORT | 8 | 1 | 下料Handler时间3 | ✓ 使用 |
| m_TPTime | USHORT | 9 | 1 | TP检测时间 | ✓ 使用 |
| m_PreGammaTime | USHORT | 10 | 1 | Pre-Gamma时间 | ✓ 使用 |
| m_FpcID | char[40] | 11 | 20 | FPC ID（40字节ASCII） | ✓ 使用 |
| m_PanelID | char[20] | 31 | 10 | Panel ID（20字节ASCII） | ✓ 使用 |
| m_EndTime1 | USHORT | 41 | 1 | 结束时间1 | ✓ 使用 |
| m_EndTime2 | USHORT | 42 | 1 | 结束时间2 | ✓ 使用 |
| m_EndTime3 | USHORT | 43 | 1 | 结束时间3 | ✓ 使用 |
| m_PreGammaContactStatus | USHORT | 44 | 1 | Pre-Gamma接触状态 | ✓ 使用 |
| m_ModelID | char[2] | 45 | 1 | 机种ID（2字节ASCII） | ✓ 使用 |
| m_IndexNum | USHORT | 46 | 1 | 索引号 | ✓ 使用 |
| m_ChNum | USHORT | 47 | 1 | 通道号 | ✓ 使用 |
| m_TpResult | USHORT | 48 | 1 | TP检测结果<br>0=未检测, 1=OK, 2=NG, 3=BYPASS | ✓ 使用 |
| m_Contact | USHORT | 49 | 1 | 接触检测结果<br>0=未检测, 1=OK, 2=NG, 3=BYPASS | ✓ 使用 |
| m_PreGamma | USHORT | 50 | 1 | Pre-Gamma结果<br>0=未检测, 1=OK, 2=NG, 3=BYPASS | ✓ 使用 |
| m_AOIInpsect | USHORT | 51 | 1 | AOI检测结果<br>0=未检测, 1=OK, 2=NG, 3=BYPASS | ✓ 使用 |
| m_TpResult2 | USHORT | 52 | 1 | TP检测结果2 | ✓ 使用 |
| m_Lumitop | USHORT | 53 | 1 | Lumitop检测结果<br>0=未检测, 1=OK, 2=NG, 3=BYPASS | ✓ 使用 |
| m_ContactCount | USHORT | 54 | 1 | 接触次数 | ✓ 使用 |
| m_LoadeHandlerNUM | USHORT | 55 | 1 | 上料Handler编号 | ✓ 使用 |
| m_UnLoadeHandlerNUM | USHORT | 56 | 1 | 下料Handler编号 | ✓ 使用 |
| m_OPView | USHORT | 57 | 1 | OPV检测结果<br>0=未检测, 1=OK, 2=NG, 3=BYPASS | ✓ 使用 |

**PLC地址映射：**
- AOI/Gamma OK: `eWordType_DFSValue1` (对应 `LOCAL_WORD_L2M_DFS_VALUE1` = 0x1E0C) + Panel编号
- ULD OK: `eWordType_UnloadOKDFSValue1` (对应 `LOCAL_WORD_L2M_UNLOAD_OK_DFS_VALUE1` = 0x1E86) + Panel编号
- ULD NG: `eWordType_UnloadNGDFSValue1` (对应 `LOCAL_WORD_L2M_UNLOAD_NG_DFS_VALUE1` = 0x1F00) + Panel编号
- Gamma NG: `eWordType_GammaNGDFSValue1` (对应 `LOCAL_WORD_L2M_GAMMA_NG_DFS_VALUE1` = 0x1E86) + Panel编号

**触发Bit：**
- `eBitType_DFSStart1` + Panel编号 (0x0CC0) - PLC设置，PC读取后设置 `eBitType_DFSEnd1` (0x1CC0)
- `eBitType_UnloadOKDFSStart1` + Panel编号 (0x0CC2) - PLC设置，PC读取后设置 `eBitType_UnloadOKDFSEnd1` (0x1CC2)
- `eBitType_UnloadNGDFSStart1` + Panel编号 (0x0CC4) - PLC设置，PC读取后设置 `eBitType_UnloadNGDFSEnd1` (0x1CC4)
- `eBitType_GammaNGDFSStart1` + Panel编号 (0x0CC2) - PLC设置，PC读取后设置 `eBitType_GammaNGDFSEnd1` (0x1CC2)

---

### 3. PanelData (Panel数据) - 11个Word

**实际使用位置：**
- `PlcThread.cpp::SumDefectCodeStart()` - 读取Panel ID
- `ManualThread.cpp` - 读取M站Panel数据
- `VisionThread.cpp` - 读取Vision Panel数据
- `MNetH::GetPanelData(eWordType_XXX, &pPanelData)` - 多个位置

| 字段名 | 类型 | 偏移 | 长度(Word) | 说明 | 代码使用 |
|--------|------|------|-----------|------|---------|
| m_PanelData | char[20] | 0 | 10 | Panel ID（20字节ASCII） | ✓ 使用 |
| m_bBufferTrayFlag | BOOL | 10 | 1 | 缓冲托盘标志（Bit字段） | ✓ 使用 |

**实际使用的WordType：**
- `eWordType_DefectCodePanelID1` + Panel编号 - 缺陷代码Panel ID
- `eWordType_ULD_OK_DefectCodePanelID1` + Panel编号 - ULD OK缺陷代码Panel ID
- `eWordType_ULD_NG_DefectCodePanelID1` + Panel编号 - ULD NG缺陷代码Panel ID
- `eWordType_GammaNGDefectCodePanelID1` + Panel编号 - Gamma NG缺陷代码Panel ID
- `eWordType_MStageAPanel` + Panel编号 - M站A Panel
- `eWordType_MStageBPanel` + Panel编号 - M站B Panel
- `eWordType_VisionPanel1` + Panel编号 - Vision Panel
- `eWordType_PreGammaPanel1` + Panel编号 - Pre-Gamma Panel

---

### 4. FpcIDData (FPC ID数据) - 20个Word

**实际使用位置：**
- `PlcThread.cpp::SumDefectCodeStart()` - 读取FPC ID
- `ManualThread.cpp` - 读取M站FPC ID
- `VisionThread.cpp` - 读取Vision FPC ID
- `MNetH::GetFpcIdData(eWordType_XXX, &pFpcData)` - 多个位置

| 字段名 | 类型 | 偏移 | 长度(Word) | 说明 | 代码使用 |
|--------|------|------|-----------|------|---------|
| m_FpcIDData | char[40] | 0 | 20 | FPC ID（40字节ASCII） | ✓ 使用 |

**实际使用的WordType：**
- `eWordType_DefectCodeFpcID1` + Panel编号 - 缺陷代码FPC ID
- `eWordType_ULD_OK_DefectCodeFpcID1` + Panel编号 - ULD OK缺陷代码FPC ID
- `eWordType_ULD_NG_DefectCodeFpcID1` + Panel编号 - ULD NG缺陷代码FPC ID
- `eWordType_GammaNGDefectCodeFpcID1` + Panel编号 - Gamma NG缺陷代码FPC ID
- `eWordType_MStageAFpcID` + Panel编号 - M站A FPC ID
- `eWordType_MStageBFpcID` + Panel编号 - M站B FPC ID
- `eWordType_VisionFpcID1` + Panel编号 - Vision FPC ID
- `eWordType_PreGammaFpcID1` + Panel编号 - Pre-Gamma FPC ID

---

### 5. DefectCodeRank (缺陷代码) - 100个Word

**实际使用位置：**
- `PlcThread.cpp::SumDefectCodeStart()` - 写入缺陷代码
- `MNetH::SetDefectRankData(eWordType_XXX, &pDefectCodeRank)` - 写入PLC

| 字段名 | 类型 | 偏移 | 长度(Word) | 说明 | 代码使用 |
|--------|------|------|-----------|------|---------|
| m_DefectCode | char[200] | 0 | 100 | 缺陷代码字符串（200字节ASCII）<br>格式：多个缺陷代码用逗号分隔 | ✓ 使用 |

**实际使用的WordType：**
- `eWordType_DefectCodeResult1` + Panel编号 - AOI缺陷代码结果
- `eWordType_UnloadOKDefectCodeResult1` + Panel编号 - ULD OK缺陷代码结果
- `eWordType_UnloadNGDefectCodeResult1` + Panel编号 - ULD NG缺陷代码结果
- `eWordType_GammaNGDefectCodeResult1` + Panel编号 - Gamma NG缺陷代码结果

**触发Bit：**
- `eBitType_DefectCodeStart1` + Panel编号 (0x0CD0) - PLC设置，PC写入后设置 `eBitType_DefectCodeEnd1` (0x1CC7)
- `eBitType_ULD_OK_DefectCodeStart1` + Panel编号 (0x0CDC) - PLC设置，PC写入后设置 `eBitType_ULD_OK_DefectCodeEnd1` (0x1CC9)
- `eBitType_ULD_NG_DefectCodeStart1` + Panel编号 (0x0CE8) - PLC设置，PC写入后设置 `eBitType_ULD_NG_DefectCodeEnd1` (0x1CCB)
- `eBitType_GammaNGDefectCodeStart1` + Panel编号 (0x0CDC) - PLC设置，PC写入后设置 `eBitType_GammaNGDefectCodeEnd1` (0x1CC9)

---

### 6. DefectGradeRank (缺陷等级) - 2个Word

**实际使用位置：**
- `PlcThread.cpp::SumDefectCodeStart()` - 写入缺陷等级
- `MNetH::SetDefectGradeRankData(eWordType_XXX, &pDefectGradeRank)` - 写入PLC

| 字段名 | 类型 | 偏移 | 长度(Word) | 说明 | 代码使用 |
|--------|------|------|-----------|------|---------|
| m_DefectGrade | char[4] | 0 | 2 | 缺陷等级（4字节ASCII） | ✓ 使用 |

**实际使用的WordType：**
- `eWordType_DefectGradeResult1` + Panel编号 - AOI缺陷等级结果
- `eWordType_UnloadOKDefectGradeResult1` + Panel编号 - ULD OK缺陷等级结果
- `eWordType_UnloadNGDefectGradeResult1` + Panel编号 - ULD NG缺陷等级结果

---

### 7. CardReaderID (读卡器ID) - 16个Word

**实际使用位置：**
- `PlcThread.cpp` - 读取读卡器ID
- `MNetH::GetCardReaderIDData(eWordType_SearchID, &pCardReaderID)` - 读取PLC

| 字段名 | 类型 | 偏移 | 长度(Word) | 说明 | 代码使用 |
|--------|------|------|-----------|------|---------|
| m_CardNo | int | 0 | 2 | 卡片号 | ✓ 使用 |
| m_UserID | char[26] | 2 | 13 | 用户ID（26字节ASCII） | ✓ 使用 |
| m_IDReaderInOut | USHORT | 15 | 1 | ID读卡器进出 | ✓ 使用 |

**触发Bit：**
- `eBitType_IDSerarchStart` (0x0701) - PLC设置，PC读取后设置 `eBitType_IDSerarchEnd`
- `eBitType_IDSerarchSend` (0x0700) - PLC发送标志

---

### 8. CardReaderPassWord (读卡器密码) - 16个Word

**实际使用位置：**
- `PlcThread.cpp` - 读取读卡器密码
- `MNetH::GetCardReaderPassWordData(eWordType_SearchPassWord, &pCardReaderPassWord)` - 读取PLC

| 字段名 | 类型 | 偏移 | 长度(Word) | 说明 | 代码使用 |
|--------|------|------|-----------|------|---------|
| m_UserPassWord | char[30] | 0 | 15 | 用户密码（30字节ASCII） | ✓ 使用 |
| m_IDReaderInOut | USHORT | 15 | 1 | ID读卡器进出 | ✓ 使用 |

**触发Bit：**
- `eBitType_PassWordSerarchStart` (0x0704) - PLC设置，PC读取后设置 `eBitType_PassWordSerarchEnd`
- `eBitType_PassWordSerarchSend` (0x0703) - PLC发送标志

---

## 三、实际使用的Bit地址（PLC → PC）

### 系统控制Bit

| Bit地址 | 宏定义 | 枚举类型 | 说明 | 代码使用 |
|---------|--------|---------|------|---------|
| 0x0000 | LOCAL_BIT_L2M_PLC_HEARTBIT | eBitType_PlcHearbit | PLC心跳信号 | ✓ 使用 |
| 0x0001 | LOCAL_BIT_L2M_PLC_START_STATUS_ADDR | eBitType_PlcStartStatus | PLC启动状态 | ✓ 使用 |
| 0x0002 | LOCAL_BIT_L2M_MODEL_START | eBitType_ModelStart | 机种启动 | ✓ 使用 |
| 0x0010 | LOCAL_BIT_L2M_ALARM_START | eBitType_AlarmStart | 报警启动 | ✓ 使用 |
| 0x0012 | LOCAL_BIT_L2M_ALARM_RESET | eBitType_AlarmReset | 报警复位 | ✓ 使用 |
| 0x0020 | LOCAL_BIT_L2M_OPERATION_START | eBitType_OperateStart | 操作启动 | ✓ 使用 |
| 0x0030 | LOCAL_BIT_L2M_AXIS_START | eBitType_AxisStart | 轴启动 | ✓ 使用 |

### 作业数据Bit

| Bit地址 | 宏定义 | 枚举类型 | 说明 | 代码使用 |
|---------|--------|---------|------|---------|
| 0x0706 | LOCAL_BIT_L2M_JOB_DATA_START1 | eBitType_JobDataStart1 | 作业数据启动1 | ✓ 使用 |
| 0x0707 | LOCAL_BIT_L2M_JOB_DATA_START2 | eBitType_JobDataStart2 | 作业数据启动2 | ✓ 使用 |
| 0x0708 | LOCAL_BIT_L2M_BC_DATA_EXIST1 | eBitType_BCDataExist1 | BC数据存在1 | ✓ 使用 |
| 0x0709 | LOCAL_BIT_L2M_BC_DATA_EXIST2 | eBitType_BCDataExist2 | BC数据存在2 | ✓ 使用 |
| 0x070A | LOCAL_BIT_L2M_UNLOAD_JOB_DATA_START1 | eBitType_UnloadJobDataStart1 | 卸载作业数据启动1 | ✓ 使用 |
| 0x070B | LOCAL_BIT_L2M_UNLOAD_JOB_DATA_START2 | eBitType_UnloadJobDataStart2 | 卸载作业数据启动2 | ✓ 使用 |
| 0x070C | LOCAL_BIT_L2M_UNLOAD_BC_DATA_EXIST1 | eBitType_UnloadBCDataExist1 | 卸载BC数据存在1 | ✓ 使用 |
| 0x070D | LOCAL_BIT_L2M_UNLOAD_BC_DATA_EXIST2 | eBitType_UnloadBCDataExist2 | 卸载BC数据存在2 | ✓ 使用 |

### DFS数据Bit

| Bit地址 | 宏定义 | 枚举类型 | 说明 | 代码使用 |
|---------|--------|---------|------|---------|
| 0x0CC0 | LOCAL_BIT_L2M_DFS_START1 | eBitType_DFSStart1 | DFS启动1 | ✓ 使用 |
| 0x0CC1 | LOCAL_BIT_L2M_DFS_START2 | eBitType_DFSStart2 | DFS启动2 | ✓ 使用 |
| 0x0CC2 | LOCAL_BIT_L2M_UNLOAD_OK_DFS_START1 | eBitType_UnloadOKDFSStart1 | 卸载OK DFS启动1 | ✓ 使用 |
| 0x0CC3 | LOCAL_BIT_L2M_UNLOAD_OK_DFS_START2 | eBitType_UnloadOKDFSStart2 | 卸载OK DFS启动2 | ✓ 使用 |
| 0x0CC4 | LOCAL_BIT_L2M_UNLOAD_NG_DFS_START1 | eBitType_UnloadNGDFSStart1 | 卸载NG DFS启动1 | ✓ 使用 |
| 0x0CC5 | LOCAL_BIT_L2M_UNLOAD_NG_DFS_START2 | eBitType_UnloadNGDFSStart2 | 卸载NG DFS启动2 | ✓ 使用 |
| 0x0CC2 | LOCAL_BIT_L2M_GAMMA_NG_DFS_START1 | eBitType_GammaNGDFSStart1 | Gamma NG DFS启动1 | ✓ 使用（Gamma系统） |
| 0x0CC3 | LOCAL_BIT_L2M_GAMMA_NG_DFS_START2 | eBitType_GammaNGDFSStart2 | Gamma NG DFS启动2 | ✓ 使用（Gamma系统） |

### 缺陷代码Bit

| Bit地址 | 宏定义 | 枚举类型 | 说明 | 代码使用 |
|---------|--------|---------|------|---------|
| 0x0CD0 | LOCAL_BIT_L2M_DEFECT_CODE_START1 | eBitType_DefectCodeStart1 | 缺陷代码启动1 | ✓ 使用 |
| 0x0CD1 | LOCAL_BIT_L2M_DEFECT_CODE_START2 | eBitType_DefectCodeStart2 | 缺陷代码启动2 | ✓ 使用 |
| 0x0CDC | LOCAL_BIT_L2M_ULD_OK_DEFECT_CODE_START1 | eBitType_ULD_OK_DefectCodeStart1 | ULD OK缺陷代码启动1 | ✓ 使用 |
| 0x0CDD | LOCAL_BIT_L2M_ULD_OK_DEFECT_CODE_START2 | eBitType_ULD_OK_DefectCodeStart2 | ULD OK缺陷代码启动2 | ✓ 使用 |
| 0x0CE8 | LOCAL_BIT_L2M_ULD_NG_DEFECT_CODE_START1 | eBitType_ULD_NG_DefectCodeStart1 | ULD NG缺陷代码启动1 | ✓ 使用 |
| 0x0CE9 | LOCAL_BIT_L2M_ULD_NG_DEFECT_CODE_START2 | eBitType_ULD_NG_DefectCodeStart2 | ULD NG缺陷代码启动2 | ✓ 使用 |
| 0x0CDC | LOCAL_BIT_L2M_GAMMA_NG_DEFECT_CODE_START1 | eBitType_GammaNGDefectCodeStart1 | Gamma NG缺陷代码启动1 | ✓ 使用（Gamma系统） |
| 0x0CDD | LOCAL_BIT_L2M_GAMMA_NG_DEFECT_CODE_START2 | eBitType_GammaNGDefectCodeStart2 | Gamma NG缺陷代码启动2 | ✓ 使用（Gamma系统） |

### 读卡器Bit

| Bit地址 | 宏定义 | 枚举类型 | 说明 | 代码使用 |
|---------|--------|---------|------|---------|
| 0x0700 | LOCAL_BIT_L2M_ID_SEARCH_SEND | eBitType_IDSerarchSend | ID搜索发送 | ✓ 使用 |
| 0x0701 | LOCAL_BIT_L2M_ID_SEARCH_START | eBitType_IDSerarchStart | ID搜索启动 | ✓ 使用 |
| 0x0703 | LOCAL_BIT_L2M_PASSOWRD_SEARCH_SEND | eBitType_PassWordSerarchSend | 密码搜索发送 | ✓ 使用 |
| 0x0704 | LOCAL_BIT_L2M_PASSOWRD_SEARCH_START | eBitType_PassWordSerarchStart | 密码搜索启动 | ✓ 使用 |

---

## 四、实际使用的Bit地址（PC → PLC）

### 系统状态Bit

| Bit地址 | 宏定义 | 枚举类型 | 说明 | 代码使用 |
|---------|--------|---------|------|---------|
| 0x1000 | LOCAL_BIT_L2M_DATA_SERVER_PC_HEARTBIT | - | 数据服务器PC心跳 | ✓ 使用 |
| 0x1002 | LOCAL_BIT_L2M_MODEL_END | eBitType_ModelEnd | 机种结束 | ✓ 使用 |
| 0x1010 | LOCAL_BIT_L2M_ALARM_END | eBitType_AlarmEnd | 报警结束 | ✓ 使用 |
| 0x1020 | LOCAL_BIT_L2M_OPERATION_END | eBitType_OperateEnd | 操作结束 | ✓ 使用 |
| 0x1030 | LOCAL_BIT_L2M_AXIS_END | eBitType_AxisEnd | 轴结束 | ✓ 使用 |

### 作业数据完成Bit

| Bit地址 | 宏定义 | 枚举类型 | 说明 | 代码使用 |
|---------|--------|---------|------|---------|
| 0x1716 | LOCAL_BIT_L2M_JOB_DATA_END1 | eBitType_JobDataEnd1 | 作业数据结束1 | ✓ 使用 |
| 0x1717 | LOCAL_BIT_L2M_JOB_DATA_END2 | eBitType_JobDataEnd2 | 作业数据结束2 | ✓ 使用 |
| 0x171A | LOCAL_BIT_L2M_UNLOAD_JOB_DATA_END1 | eBitType_UnloadJobDataEnd1 | 卸载作业数据结束1 | ✓ 使用 |
| 0x171B | LOCAL_BIT_L2M_UNLOAD_JOB_DATA_END2 | eBitType_UnloadJobDataEnd2 | 卸载作业数据结束2 | ✓ 使用 |

### DFS完成Bit

| Bit地址 | 宏定义 | 枚举类型 | 说明 | 代码使用 |
|---------|--------|---------|------|---------|
| 0x1CC0 | LOCAL_BIT_L2M_DFS_END1 | eBitType_DFSEnd1 | DFS结束1 | ✓ 使用 |
| 0x1CC1 | LOCAL_BIT_L2M_DFS_END2 | eBitType_DFSEnd2 | DFS结束2 | ✓ 使用 |
| 0x1CC2 | LOCAL_BIT_L2M_UNLOAD_OK_DFS_END1 | eBitType_UnloadOKDFSEnd1 | 卸载OK DFS结束1 | ✓ 使用 |
| 0x1CC3 | LOCAL_BIT_L2M_UNLOAD_OK_DFS_END2 | eBitType_UnloadOKDFSEnd2 | 卸载OK DFS结束2 | ✓ 使用 |
| 0x1CC4 | LOCAL_BIT_L2M_UNLOAD_NG_DFS_END1 | eBitType_UnloadNGDFSEnd1 | 卸载NG DFS结束1 | ✓ 使用 |
| 0x1CC5 | LOCAL_BIT_L2M_UNLOAD_NG_DFS_END2 | eBitType_UnloadNGDFSEnd2 | 卸载NG DFS结束2 | ✓ 使用 |
| 0x1CC2 | LOCAL_BIT_L2M_GAMMA_NG_DFS_END1 | eBitType_GammaNGDFSEnd1 | Gamma NG DFS结束1 | ✓ 使用（Gamma系统） |
| 0x1CC3 | LOCAL_BIT_L2M_GAMMA_NG_DFS_END2 | eBitType_GammaNGDFSEnd2 | Gamma NG DFS结束2 | ✓ 使用（Gamma系统） |

### 缺陷代码完成Bit

| Bit地址 | 宏定义 | 枚举类型 | 说明 | 代码使用 |
|---------|--------|---------|------|---------|
| 0x1CC7 | LOCAL_BIT_L2M_DEFECT_CODE_END1 | eBitType_DefectCodeEnd1 | 缺陷代码结束1 | ✓ 使用 |
| 0x1CC8 | LOCAL_BIT_L2M_DEFECT_CODE_END2 | eBitType_DefectCodeEnd2 | 缺陷代码结束2 | ✓ 使用 |
| 0x1CC9 | LOCAL_BIT_L2M_ULD_OK_DEFECT_CODE_END1 | eBitType_ULD_OK_DefectCodeEnd1 | ULD OK缺陷代码结束1 | ✓ 使用 |
| 0x1CCA | LOCAL_BIT_L2M_ULD_OK_DEFECT_CODE_END2 | eBitType_ULD_OK_DefectCodeEnd2 | ULD OK缺陷代码结束2 | ✓ 使用 |
| 0x1CCB | LOCAL_BIT_L2M_ULD_NG_DEFECT_CODE_END1 | eBitType_ULD_NG_DefectCodeEnd1 | ULD NG缺陷代码结束1 | ✓ 使用 |
| 0x1CCC | LOCAL_BIT_L2M_ULD_NG_DEFECT_CODE_END2 | eBitType_ULD_NG_DefectCodeEnd2 | ULD NG缺陷代码结束2 | ✓ 使用 |
| 0x1CC9 | LOCAL_BIT_L2M_GAMMA_NG_DEFECT_CODE_END1 | eBitType_GammaNGDefectCodeEnd1 | Gamma NG缺陷代码结束1 | ✓ 使用（Gamma系统） |
| 0x1CCA | LOCAL_BIT_L2M_GAMMA_NG_DEFECT_CODE_END2 | eBitType_GammaNGDefectCodeEnd2 | Gamma NG缺陷代码结束2 | ✓ 使用（Gamma系统） |

### 读卡器完成Bit

| Bit地址 | 宏定义 | 枚举类型 | 说明 | 代码使用 |
|---------|--------|---------|------|---------|
| 0x1710 | LOCAL_BIT_L2M_ID_SEARCH_RECEIVED | eBitType_IDSerarchReceived | ID搜索接收 | ✓ 使用 |
| 0x1711 | LOCAL_BIT_L2M_ID_SEARCH_END | eBitType_IDSerarchEnd | ID搜索结束 | ✓ 使用 |
| 0x1713 | LOCAL_BIT_L2M_PASSOWRD_SEARCH_RECEIVED | eBitType_PassWordSerarchReceived | 密码搜索接收 | ✓ 使用 |
| 0x1714 | LOCAL_BIT_L2M_PASSOWRD_SEARCH_END | eBitType_PassWordSerarchEnd | 密码搜索结束 | ✓ 使用 |

---

## 五、通信协议说明

### 5.1 通信方式
- **协议类型**: 三菱MELSECNET/H
- **通信方式**: 以太网（TCP/IP）
- **数据格式**: 二进制（Big-Endian）

### 5.2 数据读写规则

1. **Bit数据读写**
   - 使用 `MNetH::GetPlcBitData(type, addr)` 读取
   - 使用 `MNetH::SetPlcBitData(type, addr, bOn)` 写入
   - `type` 为枚举类型（如 `eBitType_JobDataStart1`）
   - `addr` 为偏移量（通常为 `OffSet_0 = 0`）

2. **Word数据读写**
   - 使用 `MNetH::GetPlcWordData(type, result)` 读取单个Word
   - 使用 `MNetH::SetPlcWordData(type, result)` 写入单个Word
   - 使用专用函数读取结构体：
     - `MNetH::GetJobData(type, &jobData)` - 读取JobData
     - `MNetH::GetDfsData(type, &dfsData)` - 读取DfsData
     - `MNetH::GetPanelData(type, &panelData)` - 读取PanelData
     - `MNetH::GetFpcIdData(type, &fpcData)` - 读取FpcIDData
   - 使用专用函数写入结构体：
     - `MNetH::SetDefectRankData(type, &defectCodeRank)` - 写入缺陷代码
     - `MNetH::SetDefectGradeRankData(type, &defectGradeRank)` - 写入缺陷等级

3. **字符串数据**
   - ASCII字符串按字节存储，每个Word存储2个字节
   - 字符串长度必须为偶数（不足补0）
   - 使用 `CStringSupport::ToWString()` 转换为宽字符串
   - 使用 `CStringSupport::ToAString()` 转换为ASCII字符串

### 5.3 通信时序

1. **心跳机制**
   - PLC每200ms发送心跳信号（Bit 0x0000）
   - PC每200ms发送心跳信号（Bit 0x1000）
   - 超时未收到心跳则判定通信异常

2. **作业数据流程**
   ```
   PLC设置JobDataStart Bit → PC检测到Start Bit → 
   PC读取JobData结构 → PC解析并保存 → 
   PC设置JobDataEnd Bit → PLC检测到End Bit后清除Start Bit
   ```

3. **DFS数据流程**
   ```
   PLC设置DFSStart Bit → PC检测到Start Bit → 
   PC读取DfsData结构 → PC生成DFS文件 → 
   PC设置DFSEnd Bit → PLC检测到End Bit后清除Start Bit
   ```

4. **缺陷代码流程**
   ```
   PLC设置DefectCodeStart Bit → PC检测到Start Bit → 
   PC读取PanelID和FpcID → PC查询缺陷代码 → 
   PC写入DefectCodeRank和DefectGradeRank → 
   PC设置DefectCodeEnd Bit → PLC检测到End Bit后清除Start Bit
   ```

### 5.4 状态值说明

**检测结果状态值：**
- `0` = 未检测
- `1` = OK（良品）
- `2` = NG（不良品）
- `3` = BYPASS（跳过）

**时间格式：**
- Time1: 年(高8位) + 月(低8位) / 日(高8位) + 时(低8位)
- Time2: 分(高8位) + 秒(低8位) / 毫秒高位
- Time3: 毫秒低位（如需要）

---

## 六、实际代码示例

### 读取JobData示例
```cpp
// PlcThread.cpp::JobDataStart()
JobData pJobData;
if (iType == Machine_AOI || iType == Machine_GAMMA)
    theApp.m_pEqIf->m_pMNetH->GetJobData(eWordType_JobData1 + iNum, &pJobData);
else
    theApp.m_pEqIf->m_pMNetH->GetJobData(eWordType_UnloadJobData1 + iNum, &pJobData);

// 解析CST_Operation_Mode（二进制位字段）
_itoa(pJobData.CST_Operation_Mode, strBinary, 2);
int cstMode = StringBinaryToInt(strBinary.Left(2));
int substrateType = StringBinaryToInt(strBinary.Mid(2, 2));
int cimMode = StringBinaryToInt(strBinary.Mid(4, 1));
```

### 读取DfsData示例
```cpp
// PlcThread.cpp::SumDFSDataStart()
DfsData pDfsData;
if (iOkNg == OKPanel)
    theApp.m_pEqIf->m_pMNetH->GetDfsData(eWordType_UnloadOKDFSValue1 + iNum, &pDfsData);
else
    theApp.m_pEqIf->m_pMNetH->GetDfsData(eWordType_UnloadNGDFSValue1 + iNum, &pDfsData);

// 转换时间格式
CString strStartTime = ConvertTimeToString(pDfsData.m_StartTime1, pDfsData.m_StartTime2, pDfsData.m_StartTime3);
CString strFpcID = CStringSupport::ToWString(pDfsData.m_FpcID, sizeof(pDfsData.m_FpcID));
```

### 写入缺陷代码示例
```cpp
// PlcThread.cpp::SumDefectCodeStart()
DefectCodeRank pDefectCodeRank;
DefectGradeRank pDefectGradeRank;

// 查询缺陷代码
CString strCodeGrade = theApp.SetTotalLoadResultCode(strPanelID, strFpcID, Machine_ULD);
CString strCode = responseTokens[0];
CString strGrade = responseTokens[1];

// 转换为ASCII并写入PLC
CStringSupport::ToAString(strCode, pDefectCodeRank.m_DefectCode, sizeof(pDefectCodeRank.m_DefectCode));
CStringSupport::ToAString(strGrade, pDefectGradeRank.m_DefectGrade, sizeof(pDefectGradeRank.m_DefectGrade));

theApp.m_pEqIf->m_pMNetH->SetDefectRankData(eWordType_UnloadOKDefectCodeResult1 + iNum, &pDefectCodeRank);
theApp.m_pEqIf->m_pMNetH->SetDefectGradeRankData(eWordType_UnloadOKDefectGradeResult1 + iNum, &pDefectGradeRank);
theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_ULD_OK_DefectCodeEnd1 + iNum, OffSet_0, TRUE);
```

### Bit读写示例
```cpp
// 读取Bit
BOOL bStartFlag = theApp.m_pEqIf->m_pMNetH->GetPlcBitData(eBitType_JobDataStart1 + iNum, OffSet_0);

// 写入Bit
theApp.m_pEqIf->m_pMNetH->SetPlcBitData(eBitType_JobDataEnd1 + iNum, OffSet_0, TRUE);
```

---

## 七、注意事项

1. **地址偏移计算**
   - 所有地址均为16进制
   - Panel编号从0开始
   - 多Panel数据地址 = 基地址 + Panel编号 × 偏移量
   - 例如：`eWordType_JobData1 + iNum`，其中 `iNum` 为Panel编号

2. **数据对齐**
   - 所有结构体按Word（2字节）对齐
   - int类型占2个Word（4字节）
   - char数组按字节存储，不足偶数补0

3. **字符串编码**
   - 所有字符串使用ASCII编码
   - 使用 `CStringSupport::ToWString()` 和 `CStringSupport::ToAString()` 进行转换

4. **错误处理**
   - 通信超时：检查网络连接和超时设置
   - 数据异常：检查地址映射和数据类型
   - Bit状态异常：检查通信时序和状态机逻辑

5. **系统差异**
   - `_SYSTEM_AMTAFT_` 宏定义：AMT/AFT系统（4通道AOI）
   - `_SYSTEM_GAMMA_` 宏定义：Gamma系统（12站Gamma）
   - 不同系统使用的Bit和Word地址可能不同

---

**文档版本**: v2.0（基于实际代码分析）  
**最后更新**: 2025-01-XX  
**适用系统**: PH1AFT_Ani_Data_Serever_PC  
**说明**: 本文档仅包含代码中实际使用的协议，未使用的协议定义已排除
