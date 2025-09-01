from PIL import Image

def resize_image(input_image_path, output_image_path, size):
    original_image = Image.open(input_image_path)
    print(f"原图大小: {original_image.size}")

    resized_image = original_image.resize(size, Image.LANCZOS)

    resized_image.save(output_image_path, quality=95, subsampling=0)
    print(f"已保存缩放图像到: {output_image_path}")

input_image_path = r"C:\Users\lenovo\Downloads\OIP.jpg"
output_image_path = r"C:\Users\lenovo\Downloads\OIP.jpg"
new_size = (240, 135)

resize_image(input_image_path, output_image_path, new_size)
