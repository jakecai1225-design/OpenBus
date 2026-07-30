import urllib.request
import os

os.makedirs('third_party/nlohmann_json', exist_ok=True)

url = 'https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp'
json_path = 'third_party/nlohmann_json/json.hpp'

print(f'Downloading nlohmann/json...')
urllib.request.urlretrieve(url, json_path)
print(f'Done: {json_path}')
