#include "contentdelivery.h"

#include "markdownhtmlrenderer.h"
#include <QFile>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QFileInfo>
#include <QTextDocument>
#include <QPrinter>

QString TemplateService::applyVariables(const QString& content, const QMap<QString, QString>& values) {
    QString result = content;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        result.replace(QStringLiteral("{{%1}}").arg(it.key()), it.value());
    }
    return result;
}

QMap<QString, QString> TemplateService::builtInTemplates() {
    return {
        {QStringLiteral("会议纪要"), QStringLiteral("# {{title}}\n\n- 日期：{{date}}\n- 参与人：{{attendees}}\n\n## 议题\n\n## 讨论记录\n\n## 决议与行动项\n")},
        {QStringLiteral("技术设计"), QStringLiteral("# {{title}}\n\n## 背景与目标\n\n## 需求与约束\n\n## 总体设计\n\n## 数据与接口\n\n## 安全与隐私\n\n## 测试方案\n\n## 风险与回滚\n")},
        {QStringLiteral("读书笔记"), QStringLiteral("# {{title}}\n\n- 作者：{{author}}\n- 阅读日期：{{date}}\n\n## 内容概要\n\n## 核心观点\n\n## 摘录\n\n## 思考与行动\n")},
        {QStringLiteral("周报"), QStringLiteral("# 周报 - {{date}}\n\n## 本周完成\n\n## 进行中\n\n## 下周计划\n\n## 风险与协助\n")},
        {QStringLiteral("项目计划"), QStringLiteral("# {{title}}\n\n- 负责人：{{owner}}\n- 日期：{{date}}\n\n## 背景与目标\n\n## 范围\n\n## 里程碑\n\n## 资源与依赖\n\n## 风险\n\n## 验收标准\n")}
    };
}

DocumentExporter::DocumentExporter(MarkdownHtmlRenderer& renderer) : renderer_(renderer) {}

namespace {
bool writeAtomically(const QByteArray& bytes, const QString& path, QString* error, bool overwrite, const std::atomic_bool* cancelled) {
    if (QFileInfo::exists(path) && !overwrite) {
        if (error) *error = QStringLiteral("目标文件已存在；请确认覆盖后重试");
        return false;
    }
    QSaveFile file(path);
    if (overwrite) file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    constexpr qsizetype chunkSize = 64 * 1024;
    for (qsizetype offset = 0; offset < bytes.size(); offset += chunkSize) {
        if (cancelled && cancelled->load()) {
            if (error) *error = QStringLiteral("导出已取消");
            file.cancelWriting();
            return false;
        }
        const qsizetype length = qMin(chunkSize, bytes.size() - offset);
        if (file.write(bytes.constData() + offset, length) != length) {
            if (error) *error = file.errorString();
            file.cancelWriting();
            return false;
        }
    }
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}
}

bool DocumentExporter::exportMarkdown(const QString& markdown, const QString& path, QString* error,
                                      bool overwrite, const std::atomic_bool* cancelled) const {
    if (cancelled && cancelled->load()) { if (error) *error = QStringLiteral("导出已取消"); return false; }
    return writeAtomically(markdown.toUtf8(), path, error, overwrite, cancelled);
}

bool DocumentExporter::exportHtml(const QString& markdown, const QString& path, QString* error,
                                  bool overwrite, const std::atomic_bool* cancelled) const {
    if (cancelled && cancelled->load()) { if (error) *error = QStringLiteral("导出已取消"); return false; }
    const QByteArray html = renderer_.renderLegacyHtml(markdown).toUtf8();
    return writeAtomically(html, path, error, overwrite, cancelled);
}

bool DocumentExporter::exportPdf(const QString& markdown, const QString& path, QString* error,
                                 bool overwrite, const std::atomic_bool* cancelled) const {
    if (cancelled && cancelled->load()) { if (error) *error = QStringLiteral("导出已取消"); return false; }
    if (QFileInfo::exists(path) && !overwrite) {
        if (error) *error = QStringLiteral("目标文件已存在；请确认覆盖后重试");
        return false;
    }

    QTemporaryFile temporary(QFileInfo(path).absolutePath() + QStringLiteral("/.mybase-export-XXXXXX"));
    temporary.setAutoRemove(true);
    if (!temporary.open()) {
        if (error) *error = temporary.errorString();
        return false;
    }
    const QString temporaryPath = temporary.fileName();
    temporary.close();

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(temporaryPath);
    QTextDocument document;
    document.setHtml(renderer_.renderLegacyHtml(markdown));
    document.print(&printer);

    if (cancelled && cancelled->load()) { if (error) *error = QStringLiteral("导出已取消"); return false; }
    QFile generated(temporaryPath);
    if (!generated.open(QIODevice::ReadOnly) || generated.size() <= 0) {
        if (error) *error = QStringLiteral("PDF 导出未生成有效文件");
        return false;
    }
    const QByteArray signature = generated.read(5);
    if (signature != QByteArrayLiteral("%PDF-")) {
        if (error) *error = QStringLiteral("PDF 输出格式无效");
        return false;
    }
    if (!generated.seek(0)) {
        if (error) *error = generated.errorString();
        return false;
    }

    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error) *error = output.errorString();
        return false;
    }
    while (!generated.atEnd()) {
        if (cancelled && cancelled->load()) {
            if (error) *error = QStringLiteral("导出已取消");
            output.cancelWriting();
            return false;
        }
        const QByteArray chunk = generated.read(64 * 1024);
        if (chunk.isEmpty() && generated.error() != QFileDevice::NoError) {
            if (error) *error = generated.errorString();
            output.cancelWriting();
            return false;
        }
        if (output.write(chunk) != chunk.size()) {
            if (error) *error = output.errorString();
            output.cancelWriting();
            return false;
        }
    }
    if (!output.commit()) {
        if (error) *error = output.errorString();
        return false;
    }
    return true;
}
