import cv2

def resize_video(input_file, output_file, width=240, height=135):
    """
    将 MP4 文件转换为指定分辨率（默认 240x135）的 MP4 文件。

    参数:
        input_file (str): 输入视频文件路径。
        output_file (str): 输出视频文件路径。
        width (int): 目标宽度（默认 240）。
        height (int): 目标高度（默认 135）。
    """
    try:
        # 打开视频文件
        cap = cv2.VideoCapture(input_file)
        # 获取视频的帧率和尺寸
        fps = cap.get(cv2.CAP_PROP_FPS)
        frame_width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        frame_height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        # 创建视频写入对象
        fourcc = cv2.VideoWriter_fourcc(*'mp4v')
        out = cv2.VideoWriter(output_file, fourcc, fps, (width, height))

        while cap.isOpened():
            ret, frame = cap.read()
            if not ret:
                break
            # 调整帧的分辨率
            resized_frame = cv2.resize(frame, (width, height))
            # 写入帧
            out.write(resized_frame)

        # 释放资源
        cap.release()
        out.release()
        print(f"视频已成功转换为 {width}x{height} 并保存为 {output_file}")
    except Exception as e:
        print(f"转换失败: {e}")

if __name__ == "__main__":
    # 输入文件路径
    input_file = r"G:\dong.mp4"
    # 输出文件路径
    output_file = "output_dong.mp4"

    # 调用函数调整视频分辨率
    resize_video(input_file, output_file)