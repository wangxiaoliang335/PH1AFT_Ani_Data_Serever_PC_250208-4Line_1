import os

from docx import Document


def read_docx_to_text(docx_path: str) -> str:
    """
    读取 .docx 文档内容，返回为一个字符串。
    每个段落之间使用换行分隔，自动去掉纯空行。
    """
    if not os.path.isfile(docx_path):
        raise FileNotFoundError(f"File not found: {docx_path}")

    doc = Document(docx_path)

    lines = []
    for para in doc.paragraphs:
        text = para.text.strip()
        if text:
            lines.append(text)

    return "\n".join(lines)


def main():
    # 原始 docx 文件路径
    docx_path = r"E:\CSOT_PH1_AFT151_CAMERA\5. PH1AFT_Ani_Data_Serever_PC_250208-4Line\点灯检软件通信说明.docx"

    # 输出 txt 文件路径
    txt_path = os.path.splitext(docx_path)[0] + ".txt"

    print(f"Reading docx file: {docx_path}")
    content = read_docx_to_text(docx_path)

    print("\n===== 文档内容开始 =====\n")
    print(content)
    print("\n===== 文档内容结束 =====\n")

    # 保存到 txt
    with open(txt_path, "w", encoding="utf-8") as f:
        f.write(content)

    print(f"\n文本内容已保存到: {txt_path}")


if __name__ == "__main__":
    main()

