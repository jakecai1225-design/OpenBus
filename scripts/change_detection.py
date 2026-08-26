#!/usr/bin/env python3
"""
change_detection.py - 变更检测与六角色流程触发器

用于自动识别重大变更并触发相应的角色协作流程。
运行位置：pre-commit hook / CI pipeline trigger
"""

import subprocess
import json
import sys
from pathlib import Path
from datetime import datetime


# ===== 配置参数 =====
CONFIG = {
    # 代码行数阈值
    "LINE_COUNT_THRESHOLD": 500,
    
    # 影响文件数阈值  
    "AFFECTED_FILES_THRESHOLD": 3,
    
    # 核心模块列表 (改动这些会触发更高级别评审)
    "CORE_MODULES": [
        "src/models/",
        "src/core/",
        "include/openbus_data/",
        "CMakeLists.txt",
    ],
    
    # 角色映射表
    "WORKFLOW_ROLES": {
        "major_change": ["planner", "coder", "reviewer", "compiler", "tester", "packager"],
        "medium_change": ["coder", "reviewer", "compiler"],
        "minor_change": ["coder", "reviewer"],
    }
}


def get_changed_files():
    """获取本次提交中修改的文件列表"""
    try:
        result = subprocess.run(
            ["git", "diff", "--cached", "--name-only"],
            capture_output=True, text=True, check=True
        )
        files = result.stdout.strip().split("\n")
        return [f for f in files if f]  # 过滤空字符串
    except subprocess.CalledProcessError as e:
        print(f"❌ Error getting changed files: {e}")
        return []


def calculate_line_counts():
    """计算每个文件的增删行数"""
    try:
        result = subprocess.run(
            ["git", "diff", "--cached", "--numstat"],
            capture_output=True, text=True, check=True
        )
        
        line_stats = {}
        total_added = 0
        total_deleted = 0
        
        for line in result.stdout.strip().split("\n"):
            if not line:
                continue
            parts = line.split("\t")
            if len(parts) == 3:
                added, deleted, file_path = parts
                added_count = int(added) if added != "-" else 0
                deleted_count = int(deleted) if deleted != "-" else 0
                
                line_stats[file_path] = {"added": added_count, "deleted": deleted_count}
                total_added += added_count
                total_deleted += deleted_count
        
        return {
            "files": line_stats,
            "total_added": total_added,
            "total_deleted": total_deleted,
            "net_change": total_added - total_deleted
        }
    except Exception as e:
        print(f"⚠️ Warning calculating lines: {e}")
        return None


def detect_affected_modules(changed_files):
    """识别受影响的模块"""
    affected = []
    
    for file in changed_files:
        is_core_module = False
        
        for module in CONFIG["CORE_MODULES"]:
            if file.startswith(module):
                is_core_module = True
                affected.append(module.rstrip("/"))
                break
        
        if file == "CMakeLists.txt" or "/CMakeLists.txt" in file:
            if module not in affected:
                affected.append("BuildSystem")
    
    return list(set(affected))


def analyze_code_structure(changes):
    """分析代码变更的复杂度"""
    score = 0
    
    # 1. 代码行数影响
    if changes["total_added"] > CONFIG["LINE_COUNT_THRESHOLD"]:
        score += 40
    elif changes["total_added"] > 200:
        score += 20
    
    # 2. 文件数量影响
    num_files = len(changes["files"])
    if num_files > CONFIG["AFFECTED_FILES_THRESHOLD"]:
        score += 30
    elif num_files > 2:
        score += 15
    
    # 3. 核心模块改动权重更高
    core_modules_touched = any(
        mod in str(changes.get("modules", [])) 
        for mod in CONFIG["CORE_MODULES"]
    )
    if core_modules_touched:
        score += 30
    
    return min(score, 100)  # 最大 100 分


def determine_workflow_level(score):
    """根据评分确定工作流级别"""
    if score >= 70:
        return "major_change"
    elif score >= 40:
        return "medium_change"
    else:
        return "minor_change"


def notify_roles(role_list, commit_info):
    """通知相关角色开始工作"""
    timestamp = datetime.now().isoformat()
    
    notification = {
        "type": "six_role_workflow_trigger",
        "timestamp": timestamp,
        "commit": commit_info["sha"][:8],
        "message": commit_info["subject"],
        "roles_required": role_list,
        "deadline_hours": calculate_deadline(role_list),
        "checklist": generate_checklist(role_list)
    }
    
    # 这里实际会发送 Slack/邮件/DingTalk 通知
    # 为了演示，我们输出到 JSON 文件
    output_file = Path("build/role_notification.json")
    output_file.parent.mkdir(exist_ok=True)
    
    with open(output_file, "w", encoding="utf-8") as f:
        json.dump(notification, f, indent=2, ensure_ascii=False)
    
    print(f"✅ 通知已生成：{output_file}")
    print(f"   涉及角色：{', '.join(role_list)}")
    print(f"   截止时间：{notification['deadline_hours']}小时后")
    
    return notification


def calculate_deadline(role_list):
    """根据角色数量计算合理截止时间"""
    base_hours = 24
    per_role_extra = 8
    return base_hours + (len(role_list) * per_role_extra)


def generate_checklist(role_list):
    """生成各角色检查清单"""
    checklist = {
        "planner": ["设计文档审查", "需求对齐确认"],
        "coder": ["实现完成", "单元测试通过"],
        "reviewer": ["代码审查", "静态分析"],
        "compiler": ["构建验证", "依赖检查"],
        "tester": ["测试用例执行", "覆盖率统计"],
        "packager": ["安装包生成", "发布说明"],
    }
    
    return {role: checklist.get(role, []) for role in role_list}


def main():
    """主函数入口"""
    print("=" * 60)
    print("🔍 变更检测与六角色流程触发器")
    print("=" * 60)
    
    # Step 1: 获取变更文件
    changed_files = get_changed_files()
    if not changed_files:
        print("ℹ️ 无变更文件，跳过检测")
        return 0
    
    print(f"\n📁 变更文件 ({len(changed_files)}个):")
    for f in changed_files:
        print(f"   - {f}")
    
    # Step 2: 统计代码变更
    changes = calculate_line_counts()
    if changes:
        print(f"\n📊 代码变更:")
        print(f"   新增：+{changes['total_added']}")
        print(f"   删除：-{changes['total_deleted']}")
        print(f"   净变化：{changes['net_change']}")
    
    # Step 3: 识别模块
    modules = detect_affected_modules(changed_files)
    if modules:
        print(f"\n🏗️  受影响模块:")
        for m in modules:
            print(f"   • {m}")
    
    # Step 4: 分析复杂度
    complexity_score = analyze_code_structure({
        "total_added": changes["total_added"],
        "total_deleted": changes["total_deleted"],
        "files": changed_files,
        "modules": modules
    })
    
    print(f"\n🎯 变更复杂度评分：{complexity_score}/100")
    
    # Step 5: 确定工作流级别
    workflow_level = determine_workflow_level(complexity_score)
    roles_to_notify = CONFIG["WORKFLOW_ROLES"][workflow_level]
    
    print(f"\n👥 触发的角色流程：{' → '.join(roles_to_notify)}")
    
    # Step 6: 获取提交信息
    try:
        result = subprocess.run(
            ["git", "log", "-1", "--pretty=format:%H %s"],
            capture_output=True, text=True, check=True
        )
        sha, subject = result.stdout.split(None, 1)
        commit_info = {"sha": sha, "subject": subject}
    except:
        commit_info = {"sha": "unknown", "subject": "N/A"}
    
    # Step 7: 发送通知
    notification = notify_roles(roles_to_notify, commit_info)
    
    # Step 8: 输出总结
    print("\n" + "=" * 60)
    print("📝 检测结果总结")
    print("=" * 60)
    print(f"变更级别：{workflow_level.replace('_', ' ').title()}")
    print(f"复杂度评分：{complexity_score}/100")
    print(f"触发角色：{', '.join(roles_to_notify)}")
    print(f"预计总工作量：{notification['deadline_hours']}小时")
    print(f"\n详细通知已保存至：build/role_notification.json")
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
