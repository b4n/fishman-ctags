// based off French Wikipedia example

class Animal {
  // Properties examples
  var legs: Int
  var name: String

  // Initialization function
  init(specieName: String, legCount: Int) {
    name = specieName
    legs = legCount
  }

  // Method example
  func simpleDescription() -> String {
    return "This animal is named \(name) and has \(legs) legs."
  }
}
