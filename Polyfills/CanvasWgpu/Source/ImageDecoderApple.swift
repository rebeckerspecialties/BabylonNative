import CoreGraphics
import Foundation
import ImageIO

private func unpremultiplyRGBA(_ data: UnsafeMutablePointer<UInt8>, byteCount: Int) {
    var offset = 0
    while offset + 3 < byteCount {
        let alpha = UInt32(data[offset + 3])
        if alpha == 0 {
            data[offset] = 0
            data[offset + 1] = 0
            data[offset + 2] = 0
        } else if alpha < 255 {
            data[offset] = UInt8(min(255, (UInt32(data[offset]) * 255 + alpha / 2) / alpha))
            data[offset + 1] = UInt8(min(255, (UInt32(data[offset + 1]) * 255 + alpha / 2) / alpha))
            data[offset + 2] = UInt8(min(255, (UInt32(data[offset + 2]) * 255 + alpha / 2) / alpha))
        }
        offset += 4
    }
}

@_cdecl("babylon_canvas_decode_image_rgba")
public func babylon_canvas_decode_image_rgba(
    _ data: UnsafePointer<UInt8>?,
    _ len: Int,
    _ outWidth: UnsafeMutablePointer<UInt32>?,
    _ outHeight: UnsafeMutablePointer<UInt32>?,
    _ outRgba: UnsafeMutablePointer<UnsafeMutablePointer<UInt8>?>?,
    _ outLen: UnsafeMutablePointer<Int>?
) -> Int32 {
    guard
        let data,
        len > 0,
        let outWidth,
        let outHeight,
        let outRgba,
        let outLen
    else {
        return 0
    }

    guard let encoded = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, data, len, kCFAllocatorNull) else {
        return 0
    }

    let sourceOptions = [
        kCGImageSourceShouldCache: false,
        kCGImageSourceShouldAllowFloat: false,
    ] as CFDictionary
    guard
        let source = CGImageSourceCreateWithData(encoded, sourceOptions),
        let image = CGImageSourceCreateImageAtIndex(source, 0, sourceOptions)
    else {
        return 0
    }

    let width = image.width
    let height = image.height
    guard width > 0, height > 0, width <= Int(UInt32.max), height <= Int(UInt32.max) else {
        return 0
    }

    let bytesPerPixel = 4
    guard
        width <= Int.max / bytesPerPixel,
        height <= Int.max / (width * bytesPerPixel)
    else {
        return 0
    }

    let bytesPerRow = width * bytesPerPixel
    let byteCount = bytesPerRow * height
    let decoded = UnsafeMutablePointer<UInt8>.allocate(capacity: byteCount)

    guard let colorSpace = CGColorSpace(name: CGColorSpace.sRGB) else {
        decoded.deallocate()
        return 0
    }

    let bitmapInfo = CGBitmapInfo.byteOrder32Big.rawValue | CGImageAlphaInfo.premultipliedLast.rawValue
    guard let context = CGContext(
        data: decoded,
        width: width,
        height: height,
        bitsPerComponent: 8,
        bytesPerRow: bytesPerRow,
        space: colorSpace,
        bitmapInfo: bitmapInfo
    ) else {
        decoded.deallocate()
        return 0
    }

    let bounds = CGRect(x: 0, y: 0, width: width, height: height)
    context.clear(bounds)
    context.draw(image, in: bounds)
    unpremultiplyRGBA(decoded, byteCount: byteCount)

    outWidth.pointee = UInt32(width)
    outHeight.pointee = UInt32(height)
    outRgba.pointee = decoded
    outLen.pointee = byteCount
    return 1
}

@_cdecl("babylon_canvas_free_bytes")
public func babylon_canvas_free_bytes(_ data: UnsafeMutablePointer<UInt8>?, _ _: Int) {
    data?.deallocate()
}
