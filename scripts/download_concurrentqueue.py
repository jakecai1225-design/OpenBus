#!/usr/bin/env python3
"""下载 moodycamel::ConcurrentQueue 单头文件库"""
import os, urllib.request

os.makedirs('third_party/concurrentqueue', exist_ok=True)

url = 'https://raw.githubusercontent.com/cameron314/concurrentqueue/master/concurrentqueue.h'
path = 'third_party/concurrentqueue/concurrentqueue.h'

print(f'Downloading concurrentqueue.h...')
urllib.request.urlretrieve(url, path)
print(f'Done: {path}')

# 下载 blockingconcurrentqueue.h（提供阻塞式 dequeue）
url2 = 'https://raw.githubusercontent.com/cameron314/concurrentqueue/master/blockingconcurrentqueue.h'
path2 = 'third_party/concurrentqueue/blockingconcurrentqueue.h'
print(f'Downloading blockingconcurrentqueue.h...')
urllib.request.urlretrieve(url2, path2)
print(f'Done: {path2}')
