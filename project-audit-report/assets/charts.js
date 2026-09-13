// charts.js — VisionFlowPlatform 项目审核报告图表（IIFE，颜色取自主题变量）
(function () {
  'use strict';

  var style = getComputedStyle(document.documentElement);
  var accent = style.getPropertyValue('--accent').trim();
  var accent2 = style.getPropertyValue('--accent2').trim();
  var ink = style.getPropertyValue('--ink').trim();
  var muted = style.getPropertyValue('--muted').trim();
  var rule = style.getPropertyValue('--rule').trim();
  var bg2 = style.getPropertyValue('--bg2').trim();
  var danger = style.getPropertyValue('--danger').trim();
  var warn = style.getPropertyValue('--warn').trim();
  var ok = style.getPropertyValue('--ok').trim();

  function noop() {}
  function initRender(el) {
    return echarts.init(el, null, { renderer: 'svg' });
  }
  function baseTooltip() {
    return { trigger: 'item' };
  }

  // ---------- 图1：六维评分雷达 ----------
  var radar = initRender(document.getElementById('chart-radar'));
  radar.setOption({
    animation: false,
    tooltip: baseTooltip(),
    radar: {
      indicator: [
        { name: '功能完整度', max: 10 },
        { name: '架构设计与扩展性', max: 10 },
        { name: '代码质量与资源管理', max: 10 },
        { name: '线程并发安全', max: 10 },
        { name: '安全性', max: 10 },
        { name: '操作性与UI设计', max: 10 },
        { name: '构建与可运行性', max: 10 },
        { name: '工程化与版本管理', max: 10 }
      ],
      radius: '66%',
      splitArea: { areaStyle: { color: [bg2, 'rgba(0,0,0,0)', bg2, 'rgba(0,0,0,0)', bg2] } },
      axisName: { color: ink, fontSize: 12 },
      splitLine: { lineStyle: { color: rule } },
      splitArea: { areaStyle: { color: ['rgba(0,0,0,0)', 'rgba(0,0,0,0)'] } }
    },
    series: [{
      type: 'radar',
      data: [
        {
          value: [9.0, 7.5, 6.0, 5.0, 5.5, 6.5, 8.5, 4.0],
          name: '评分',
          areaStyle: { color: accent + '33' },
          lineStyle: { color: accent, width: 2 },
          itemStyle: { color: accent }
        }
      ]
    }]
  });
  window.addEventListener('resize', function () { radar.resize(); });

  // ---------- 图2：需求覆盖度（按功能组：已实现 / 需求项） ----------
  var cov = initRender(document.getElementById('chart-coverage'));
  cov.setOption({
    animation: false,
    tooltip: { trigger: 'axis', appendToBody: true },
    legend: { data: ['已实现', '需求项'], textStyle: { color: ink }, top: 0 },
    grid: { left: 10, right: 20, top: 40, bottom: 10, containLabel: true },
    xAxis: { type: 'value', max: 8, axisLabel: { color: muted }, splitLine: { lineStyle: { color: rule } } },
    yAxis: {
      type: 'category',
      data: ['采集与图像 (P0)', '预处理 (P1)', '分析测量 (P2)', '输出控制 (P3)'],
      axisLabel: { color: ink },
      axisLine: { lineStyle: { color: rule } }
    },
    series: [
      { name: '已实现', type: 'bar', data: [4, 4, 6, 4], barMaxWidth: 16, itemStyle: { color: accent, borderRadius: 4 } },
      { name: '需求项', type: 'bar', data: [4, 4, 6, 4], barMaxWidth: 16, itemStyle: { color: muted + '55', borderRadius: 4 } }
    ]
  });
  window.addEventListener('resize', function () { cov.resize(); });

  // ---------- 图3：发现问题严重级别分布（环形） ----------
  var risk = initRender(document.getElementById('chart-risk'));
  risk.setOption({
    animation: false,
    tooltip: baseTooltip(),
    legend: { textStyle: { color: ink }, bottom: 0 },
    series: [{
      type: 'pie',
      radius: ['48%', '72%'],
      center: ['50%', '46%'],
      avoidLabelOverlap: true,
      itemStyle: { borderRadius: 6, borderColor: '#fff', borderWidth: 2 },
      label: { color: ink, formatter: '{b}\n{d}%' },
      data: [
        { value: 7, name: '高危', itemStyle: { color: danger } },
        { value: 10, name: '中危', itemStyle: { color: warn } },
        { value: 6, name: '低危/建议', itemStyle: { color: ok } }
      ]
    }]
  });
  window.addEventListener('resize', function () { risk.resize(); });
})();