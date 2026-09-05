-- ============================================================
-- 鸿蒙硬件传感数据后端 - 数据库表结构
-- 云服务器部署 MySQL 时，先执行本文件一次即可。
-- 建表语句同时也内置在 MySQLSensorDataStore::initialize() 中，
-- 程序启动连接成功后会自动保证表存在。
-- ============================================================

CREATE DATABASE IF NOT EXISTS inking_backend
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_general_ci;

USE inking_backend;

-- 传感器原始上报数据
-- 客户端 SEND_SENSE_DATA/温度,湿度 上报的数据会写入本表；
-- MINUTE/HOUR/DAY 三张表格由服务端用 SQL 按时间桶聚合得到，
-- 桶大小分别为 1 分钟 / 1 小时 / 1 天，每页固定返回 10 个点。
CREATE TABLE IF NOT EXISTS sense_data
(
    id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '主键',
    device_id   VARCHAR(64)     NOT NULL DEFAULT 'default' COMMENT '设备标识(协议未带时固定 default)',
    temperature DECIMAL(6, 2)   NOT NULL COMMENT '温度(℃)',
    humidity    DECIMAL(6, 2)   NOT NULL COMMENT '湿度(%)',
    sample_time BIGINT          NOT NULL COMMENT '采集时间(Unix 毫秒)',
    created_at  DATETIME(3)     NOT NULL DEFAULT CURRENT_TIMESTAMP(3) COMMENT '入库时间',
    PRIMARY KEY (id),
    KEY idx_device_sample_time (device_id, sample_time)
) ENGINE = InnoDB
  DEFAULT CHARSET = utf8mb4 COMMENT ='传感器上报数据';

-- 可选：如果后续数据量变大、实时表格聚合变慢，
-- 可以新增 sense_data_minute 聚合表，用定时任务/上报时增量维护。
