import urllib.request
import os
import zipfile

os.makedirs('third_party', exist_ok=True)
os.makedirs('tmp', exist_ok=True)

url = 'https://github.com/gabime/spdlog/archive/refs/tags/v1.14.1.zip'
zip_path = 'tmp/spdlog.zip'

print(f'Downloading spdlog v1.14.1...')
urllib.request.urlretrieve(url, zip_path)
print(f'Done: {zip_path}')

extract_dir = 'third_party/spdlog'
print(f'Extracting to {extract_dir}...')
with zipfile.ZipFile(zip_path, 'r') as zip_ref:
    zip_ref.extractall('tmp')

# Rename extracted folder
import shutil
shutil.move('tmp/spdlog-1.14.1', extract_dir)
os.remove(zip_path)
print(f'Extraction complete!')
