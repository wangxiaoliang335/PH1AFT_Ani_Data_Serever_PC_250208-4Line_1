-- ============================================================================
-- 点灯检测软件数据库表结构
-- 基于: 点灯数据库表说明.xlsx
-- 创建时间: 2026-01-31
-- 数据库: SQL Server
-- ============================================================================

-- ============================================================================
-- 1. 屏表 IVS_LCD_InspectionResult
-- 说明: 存储每块屏的检测结果信息
-- ============================================================================
IF NOT EXISTS (SELECT * FROM sysobjects WHERE name='IVS_LCD_InspectionResult' AND xtype='U')
CREATE TABLE IVS_LCD_InspectionResult (
    -- 主键
    SysID                       INT IDENTITY(1,1) PRIMARY KEY,
    GUID                        UNIQUEIDENTIFIER NOT NULL DEFAULT NEWID(),
    
    -- 屏基本信息
    ScreenID                    VARCHAR(100),           -- 屏二维码
    DeviceID                    VARCHAR(50),            -- AOI检测设备号
    PlatformID                  INT DEFAULT 0,          -- 屏在工位的位置号 (0,1,2,3...)
    ModelName                   VARCHAR(100),           -- 检测模板名(配方名)
    UniqueID                    VARCHAR(100),           -- ICW生成的唯一标识符
    MarkID                      VARCHAR(50),            -- 屏号
    MainAoiFixID                VARCHAR(20),            -- 主检治具号
    
    -- 检测时间
    StartTime                   DATETIME,               -- 检测开始时间
    StopTime                    DATETIME,               -- 检测结束时间
    
    -- 检测结果
    Status                      VARCHAR(50),            -- 检测状态(正常/图像采集失败/点灯异常等)
    AOIResult                   VARCHAR(50),            -- 主检结果(OK/NG/BrightDot/BlackDot/Line/Mura/Block/BM/Other)
    ReviewResult_Worker         VARCHAR(50),            -- 人工复判结果
    ReviewResult_Machine        VARCHAR(50),            -- 自动复检结果
    AllPerspectiveResult        VARCHAR(50),            -- 全视角检测结果
    
    -- 定位信息
    LocateShiftX                FLOAT,                  -- 定位偏移量X
    LocateShiftY                FLOAT,                  -- 定位偏移量Y
    LocateAngle                 FLOAT,                  -- 定位偏移角度
    
    -- 图像尺寸
    RawImageXLen                INT,                    -- 原始图X方向长度(像素)
    RawImageYLen                INT,                    -- 原始图Y方向长度(像素)
    GridImageXLen               INT,                    -- 栅格图X方向长度(像素)
    GridImageYLen               INT,                    -- 栅格图Y方向长度(像素)
    
    -- 物理尺寸
    PanelPhysicalXLen           FLOAT,                  -- 屏X方向毫米数
    PanelPhysicalYLen           FLOAT,                  -- 屏Y方向毫米数
    
    -- 灰度值
    L255_Grayscale              FLOAT,                  -- 白画面灰度
    L0_Grayscale                FLOAT,                  -- 黑画面灰度
    
    -- 缺陷分类结果 - 主检
    Code_AOI                    VARCHAR(50),            -- 主检缺陷Code
    Grade_AOI                   VARCHAR(10),            -- 主检产品等级(S,A,B等)
    Level_AOI                   VARCHAR(10),            -- 主检缺陷Level
    DefClass_AOI                VARCHAR(100),           -- 主检缺陷分类大类
    DefName_AOI                 VARCHAR(100),           -- 主检缺陷分类名字
    Pats_AOI                    VARCHAR(1000),          -- 主检画面名字符串(分号分隔)
    
    -- 缺陷分类结果 - 全视角
    Code_AllView                VARCHAR(50),
    Grade_AllView               VARCHAR(10),
    Level_AllView               VARCHAR(10),
    DefClass_AllView            VARCHAR(100),
    DefName_AllView             VARCHAR(100),
    
    -- 缺陷分类结果 - 自动复检
    Code_AutoReview             VARCHAR(50),
    Grade_AutoReview            VARCHAR(10),
    Level_AutoReview            VARCHAR(10),
    DefClass_AutoReview         VARCHAR(100),
    DefName_AutoReview          VARCHAR(100),
    
    -- 缺陷分类结果 - 人工复判
    Code_ManualReview           VARCHAR(50),
    Grade_ManualReview          VARCHAR(10),
    Level_ManualReview          VARCHAR(10),
    
    -- 工站信息
    Station_AllView             VARCHAR(50),
    Station_AutoReview          VARCHAR(50),
    Station_ManualReview        VARCHAR(50),            -- 人工复判工站号
    
    -- 操作员信息
    OperatorID                  VARCHAR(50),            -- 操作员ID
    Operator_ManualReview       VARCHAR(50),            -- 人工复判操作员ID
    
    -- 时间戳
    StartTime_AllView           DATETIME,
    StopTime_AllView            DATETIME,
    StartTime_AutoReview        DATETIME,
    StopTime_AutoReview         DATETIME,
    StartTime_ManualReview      DATETIME,               -- 人工复判开始时间
    StopTime_ManualReview       DATETIME,               -- 人工复判结束时间
    
    -- 治具信息
    ReviewFixID_Worker          VARCHAR(20),
    ReviewFixID_Machine         VARCHAR(20),
    AllPerspectiveFixID         VARCHAR(20),
    
    -- 其他
    ShiftID                     VARCHAR(50),
    LotID                       VARCHAR(50),
    ProcessType                 VARCHAR(50),
    LineID                      VARCHAR(50),
    LocalIP                     VARCHAR(20),
    CIMMode                     VARCHAR(50),
    RuncardID                   VARCHAR(50),
    CassetteID                  VARCHAR(50),
    SlotID                      VARCHAR(50),
    ProductID                   VARCHAR(50),
    DevUnitID                   VARCHAR(50),
    
    -- XML扩展信息
    XMLInfo                     NVARCHAR(MAX),          -- XML格式的扩展信息
    
    -- 创建时间索引
    CreateTime                  DATETIME DEFAULT GETDATE()
);
GO

-- 创建索引
CREATE INDEX IX_InspectionResult_GUID ON IVS_LCD_InspectionResult(GUID);
CREATE INDEX IX_InspectionResult_ScreenID ON IVS_LCD_InspectionResult(ScreenID);
CREATE INDEX IX_InspectionResult_UniqueID ON IVS_LCD_InspectionResult(UniqueID);
CREATE INDEX IX_InspectionResult_StartTime ON IVS_LCD_InspectionResult(StartTime);
CREATE INDEX IX_InspectionResult_AOIResult ON IVS_LCD_InspectionResult(AOIResult);
GO


-- ============================================================================
-- 2. 缺陷表 IVS_LCD_AOIResult
-- 说明: 存储每个缺陷的详细信息
-- ============================================================================
IF NOT EXISTS (SELECT * FROM sysobjects WHERE name='IVS_LCD_AOIResult' AND xtype='U')
CREATE TABLE IVS_LCD_AOIResult (
    -- 主键
    SysID                               INT IDENTITY(1,1) PRIMARY KEY,
    
    -- 关联屏表
    GUID_IVS_LCD_InspectionResult       UNIQUEIDENTIFIER NOT NULL,  -- 关联屏表GUID
    
    -- 缺陷基本信息
    DefectIndex                         INT,                -- 缺陷编号
    Type                                VARCHAR(50),        -- 缺陷类型(BrightDot/BlackDot/Line/Mura/Block/BM/Other)
    PatternID                           INT,                -- 缺陷所在画面号(0~29)
    PatternName                         VARCHAR(50),        -- 缺陷所在画面名(White/Gray等)
    InspType                            VARCHAR(20),        -- 检出工位(MainAOI/AllView)
    
    -- 缺陷位置(像素坐标)
    Pos_x                               INT,                -- 缺陷中心X坐标
    Pos_y                               INT,                -- 缺陷中心Y坐标
    Pos_width                           INT,                -- 缺陷宽度
    Pos_height                          INT,                -- 缺陷高度
    
    -- 缺陷物理尺寸(mm)
    TrueSize                            FLOAT,              -- 缺陷尺寸 = (长+短)/2
    TrueDiameter                        FLOAT,              -- 缺陷直径 = 长轴长
    TrueLongSize                        FLOAT,              -- 缺陷长尺寸(长轴长)
    TrueShortSize                       FLOAT,              -- 缺陷短尺寸(短轴长)

    -- 原始图特征(像素) - 点灯数据库表说明.xlsx: 需要新增
    OriArea                             INT,                -- 原始图面积(像素个数)
    OriLongSize                         INT,                -- 原始图长轴长度(像素)
    OriShortSize                        INT,                -- 原始图短轴长度(像素)
    
    -- 缺陷灰度特征
    GrayScale                           FLOAT,              -- 缺陷灰度值
    GrayScale_BK                        FLOAT,              -- 缺陷背景灰度值
    GrayScaleDiff                       FLOAT,              -- 缺陷灰度差
    GrayscaleMean                       FLOAT,              -- 缺陷平均灰度
    GrayscaleMin                        FLOAT,              -- 缺陷最小灰度
    GrayscaleMax                        FLOAT,              -- 缺陷最大灰度
    
    -- 缺陷几何特征
    Area                                INT,                -- 缺陷面积(像素个数)
    Roundness                           FLOAT,              -- 圆度
    MajorAxisAngle                      FLOAT,              -- 主轴角度
    JND                                 FLOAT,              -- 色斑JND值
    
    -- 缺陷层级
    Layer                               VARCHAR(50),        -- 缺陷层(TP/OCA/U-POL/Cell/D-POL等)
    
    -- 复判结果
    ReviewResult_Worker                 VARCHAR(50),        -- 人工复判结果
    ReviewResult_Machine                VARCHAR(50),        -- 自动复检结果
    MachineReviewDefectName             VARCHAR(100),       -- 自动复检精细缺陷名
    
    -- 缺陷分类 - 主检
    Code_AOI                            VARCHAR(50),        -- 主检缺陷Code
    Grade_AOI                           VARCHAR(10),        -- 主检缺陷等级
    Level_AOI                           VARCHAR(10),        -- 主检缺陷Level
    DefClass_AOI                        VARCHAR(100),       -- 主检缺陷分类大类(新增)
    DefName_AOI                         VARCHAR(100),       -- 主检缺陷分类名字(新增)

    -- 算法/特征信息(主检) - 点灯数据库表说明.xlsx: 需要新增
    AlgName                             VARCHAR(100),       -- 算法名
    AlgID                               INT,                -- 算法ID
    ReasonCode                          VARCHAR(200),       -- ReasonCode
    FeatureName                         VARCHAR(100),       -- 规则特征名
    FeatureMin                          VARCHAR(100),       -- 特征规格下限
    FeatureMax                          VARCHAR(100),       -- 特征规格上限
    FeatureUnit                         VARCHAR(100),       -- 特征单位
    FeatureValue                        VARCHAR(100),       -- 特征值
    
    -- 缺陷分类 - 自动复检
    Code_AutoReview                     VARCHAR(50),
    Grade_AutoReview                    VARCHAR(10),
    Level_AutoReview                    VARCHAR(10),
    
    -- 缺陷分类 - 人工复判
    Code_ManualReview                   VARCHAR(50),
    Grade_ManualReview                  VARCHAR(10),
    Level_ManualReview                  VARCHAR(10),
    
    -- 缺陷图路径
    ImagePath                           VARCHAR(500),       -- 缺陷小图路径
    
    -- XML扩展信息
    XMLInfo                             NVARCHAR(MAX),      -- XML格式扩展信息
    
    -- 自动复检字段
    DefColor                            VARCHAR(100),       -- 缺陷主颜色(White/Red/Green/Blue)
    DefColorValue                       FLOAT,              -- 缺陷主颜色亮度
    DefClass_AutoReview                 VARCHAR(100),       -- 自动复检缺陷分类大类
    DefName_AutoReview                  VARCHAR(100),       -- 自动复检缺陷分类名字
    PointType                           VARCHAR(100),       -- 点缺陷类型(PixBlkDot/BriDot/ForeignDot)
    
    -- 创建时间
    CreateTime                          DATETIME DEFAULT GETDATE()
);
GO

-- 创建索引
CREATE INDEX IX_AOIResult_GUID ON IVS_LCD_AOIResult(GUID_IVS_LCD_InspectionResult);
CREATE INDEX IX_AOIResult_Type ON IVS_LCD_AOIResult(Type);
CREATE INDEX IX_AOIResult_DefectIndex ON IVS_LCD_AOIResult(DefectIndex);
GO

-- 创建外键约束
ALTER TABLE IVS_LCD_AOIResult
ADD CONSTRAINT FK_AOIResult_InspectionResult
FOREIGN KEY (GUID_IVS_LCD_InspectionResult)
REFERENCES IVS_LCD_InspectionResult(GUID);
GO

-- ============================================================================
-- 查询示例
-- ============================================================================

-- 查询某屏的所有缺陷
-- SELECT d.* FROM IVS_LCD_AOIResult d
-- INNER JOIN IVS_LCD_InspectionResult r ON d.GUID_IVS_LCD_InspectionResult = r.GUID
-- WHERE r.ScreenID = 'xxxxx';

-- 按日期统计检测结果
-- SELECT CONVERT(DATE, StartTime) as InspDate, AOIResult, COUNT(*) as Cnt
-- FROM IVS_LCD_InspectionResult
-- GROUP BY CONVERT(DATE, StartTime), AOIResult
-- ORDER BY InspDate DESC;

PRINT '数据库表创建完成!';
GO

