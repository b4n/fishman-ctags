// from English Wikipedia

// This is one line comment using two slashes

/* This is also a comment,
   but written over multiple lines */

/* Multiline comments
   /* can be nested! */
   Thus, code containing multiline
   comments can be blocked out
*/

// Swift variables are declared with "var"
// This is followed by a name, a type, and a value
var explicitDouble: Double = 70

// If the type is omitted, Swift will infer it from
// the variable's initial value
var implicitInteger = 70
var implicitDouble = 70.0
var 國 = "美國"
var 🌎 = "🐝🐙🐧🐨🐸"

// Swift constants are declared with "let"
// followed by a name, a type, and a value
let numberOfBananas: Int = 10

// Like variables, if the type of a constant is omitted,
// Swift will infer it from the constant's value
let numberOfApples = 3
let numberOfOranges = 5

// Values of variables and constants can both be
// interpolated in strings as follows
let appleSummary = "I have \(numberOfApples) apples."
let fruitSummary = "I have \(numberOfApples + numberOfOranges) pieces of fruit."

// In "playgrounds", code can be placed in the global scope
print("Hello, world")

// This is an array variable
var fruits = ["mango", "kiwi", "avocado"]

// Example of an if statement; .isEmpty, .count
if fruits.isEmpty {
    print("No fruits in my array.")
} else {
    print("There are \(fruits.count) items in my array")
}

// Define a dictionary with four items:
// Each item has a person's name and age
let people = ["Anna": 67, "Beto": 8, "Jack": 33, "Sam": 25]

// Now use Swift's flexible enumerator system
// to extract both values in one loop
for (name, age) in people {
    print("\(name) is \(age) years old.")
}

// Functions and methods are both declared with the
// "func" syntax, and the return type is specified with ->
func sayHello(personName: String) -> String {
    let greeting = "Hello, \(personName)!"
    return greeting
}

// prints "Hello, Dilan!"
print(sayHello(personName: "Dilan"))

// Parameter names can be made external and required
// for calling.
// The external name can be the same as the parameter
// name (by doubling up)
// - or it can be defined separately.

func sayAge(personName personName: String, personAge age: Int) -> String {
    let result = "\(personName) is \(age) years old."
    return result
}

// We can also specify the name of the parameter

print(sayAge(personName: "Dilan", personAge: 42))
